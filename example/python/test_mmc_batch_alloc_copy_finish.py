#!/usr/bin/env python
# coding=utf-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# MemCache_Hybrid is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

"""Test script for the batch_alloc / batch_copy / batch_copy_layers / batch_write_finish /
get_key_info / batch_get_key_info / batch_add_lease / batch_remove_lease interface group.

Flow summary:
  Write side: batch_alloc -> batch_copy (write) -> batch_write_finish
  Read  side: batch_get_key_info -> batch_add_lease -> batch_copy (read) -> batch_remove_lease
"""

import unittest

import torch
import torch_npu

from memcache_hybrid import (
    DistributedObjectStore,
    KeyInfo,
    L2G,
    G2L,
    G2H,
    H2G,
    AUTO,
)
import acl

acl.init()
device_count, _ = acl.rt.get_device_count()
print("device count:", device_count)
acl.rt.set_device(device_count - 1)

MEDIA_HBM = 0
MEDIA_DRAM = 1

NUM_LAYERS = 4
BLOCK_SIZE = 1024
BLOB_SIZE = NUM_LAYERS * BLOCK_SIZE


class TestBatchAllocCopyFinish(unittest.TestCase):
    """End-to-end test for the GVA-based batch write/read flow."""

    @classmethod
    def setUpClass(cls):
        cls.store = DistributedObjectStore()
        ret = cls.store.init(device_count - 1)
        assert ret == 0, f"store.init failed: {ret}"

    @classmethod
    def tearDownClass(cls):
        cls.store.close()
        print("object store destroyed")

    def _alloc_hbm_tensors(self, count, fill_value):
        tensors = []
        for i in range(count):
            t = torch.full(size=(BLOB_SIZE,), fill_value=fill_value, dtype=torch.uint8, device="npu")
            tensors.append(t)
        return tensors

    def test_batch_copy_layers_write_finish_read(self):
        """batch_copy_layers variant: multi-layer write -> batch_write_finish -> read back."""
        keys = [f"layers-key-{i}" for i in range(2)]
        total_sizes = [BLOB_SIZE] * len(keys)
        layer_sizes = [[BLOCK_SIZE] * NUM_LAYERS for _ in range(len(keys))]

        # 1) batch_alloc.
        gvas = self.store.batch_alloc(keys, total_sizes)
        for gva in gvas:
            self.assertNotEqual(gva, 0)

        # 2) Prepare layered source tensors: [NUM_LAYERS][BLOCK_SIZE] per key.
        src_layers = []
        for k in range(len(keys)):
            layers = []
            for l in range(NUM_LAYERS):
                fill = (k + 1) * 10 + l
                t = torch.full(size=(BLOCK_SIZE,), fill_value=fill, dtype=torch.uint8, device="npu")
                layers.append(t)
            src_layers.append(layers)
        torch.npu.current_stream().synchronize()
        # 3) batch_copy_layers write.
        print("\n--- write: src layer data_ptr() ---")
        for k in range(len(keys)):
            for l in range(NUM_LAYERS):
                ptr = src_layers[k][l].data_ptr()
                print(f"  key={keys[k]} layer={l} gva={hex(gvas[k])} src_ptr={hex(ptr)} size={layer_sizes[k][l]}")
        write_ret = self.store.batch_copy_layers(
            gva_ptrs=gvas,
            buffer_ptrs=[[layer.data_ptr() for layer in layers] for layers in src_layers],
            sizes=layer_sizes,
            direct=L2G,
        )
        self.assertEqual(write_ret, 0, f"batch_copy_layers write failed: {write_ret}")

        # 4) batch_write_finish.
        finish_results = self.store.batch_write_finish(keys=keys, res=[0] * len(keys))
        self.assertEqual(finish_results, [0] * len(keys))

        # 5) batch_add_lease.
        lease_results = self.store.batch_add_lease(keys=keys)
        self.assertEqual(lease_results, [0] * len(keys))

        # 6) batch_copy_layers read into zero-filled destination layers.
        dst_layers = []
        for k in range(len(keys)):
            layers = [torch.zeros(size=(BLOCK_SIZE,), dtype=torch.uint8, device="npu") for _ in range(NUM_LAYERS)]
            dst_layers.append(layers)

        torch.npu.current_stream().synchronize()
        # 6) batch_copy_layers read into zero-filled destination layers.
        print("\n--- read: dst layer data_ptr() ---")
        for k in range(len(keys)):
            for l in range(NUM_LAYERS):
                ptr = dst_layers[k][l].data_ptr()
                print(f"  key={keys[k]} layer={l} gva={hex(gvas[k])} dst_ptr={hex(ptr)} size={layer_sizes[k][l]}")
        read_ret = self.store.batch_copy_layers(
            gva_ptrs=gvas,
            buffer_ptrs=[[layer.data_ptr() for layer in layers] for layers in dst_layers],
            sizes=layer_sizes,
            direct=G2L,
        )
        self.assertEqual(read_ret, 0, f"batch_copy_layers read failed: {read_ret}")

        # 7) batch_remove_lease.
        self.assertEqual(self.store.batch_remove_lease(keys=keys), 0)

        # 8) Verify each layer.
        for k in range(len(keys)):
            for l in range(NUM_LAYERS):
                self.assertTrue(
                    torch.equal(src_layers[k][l], dst_layers[k][l]),
                    f"layer mismatch at key={keys[k]} layer={l}",
                )

        # Cleanup.
        self.store.remove_batch(keys)


if __name__ == "__main__":
    unittest.main()
