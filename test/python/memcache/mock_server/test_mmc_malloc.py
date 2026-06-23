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

import multiprocessing
import queue
import threading
import time
import traceback
import unittest
import uuid

import acl
import torch
import torch_npu

from memcache_hybrid import DistributedObjectStore, L2G, G2L
from memcache_hybrid import MetaService


WORKER_DEFAULT_SHAPE = (61, 10, 1024)


def start_meta_service():
    try:
        MetaService.main()
    except Exception as exc:
        print(f"MetaService failed: {exc}")


def set_device(device_id):
    acl.init()
    ret = acl.rt.set_device(device_id)
    if ret != 0:
        raise RuntimeError(f"set_device {device_id} failed, ret={ret}")


def sync_stream():
    torch_npu.npu.current_stream().synchronize()


def init_store(device_id):
    set_device(device_id)
    store = DistributedObjectStore()
    res = store.init(device_id)
    if res != 0:
        raise RuntimeError(f"store.init({device_id}) failed, ret={res}")
    return store


def build_pattern_write_tensors(num_tensors=4, shape=None):
    shape = shape or WORKER_DEFAULT_SHAPE
    write_tensors = []
    buffers = []
    sizes = []
    expected_sums = []
    numel = torch.Size(shape).numel()
    for idx in range(num_tensors):
        fill_value = idx + 1
        tensor = torch.full(
            size=shape,
            fill_value=fill_value,
            dtype=torch.uint8,
            device=torch.device("npu"),
        )
        write_tensors.append(tensor)
        buffers.append(tensor.data_ptr())
        sizes.append(tensor.element_size() * tensor.nelement())
        expected_sums.append(fill_value * numel)
    sync_stream()
    return write_tensors, buffers, sizes, expected_sums


def build_read_tensors(num_tensors=4, shape=None):
    shape = shape or WORKER_DEFAULT_SHAPE
    read_tensors = []
    buffers = []
    sizes = []
    for _ in range(num_tensors):
        tensor = torch.empty(
            size=shape,
            dtype=torch.uint8,
            device=torch.device("npu"),
        )
        read_tensors.append(tensor)
        buffers.append(tensor.data_ptr())
        sizes.append(tensor.element_size() * tensor.nelement())
    return read_tensors, buffers, sizes


def wait_process_barrier(barrier, stage_name, timeout_seconds):
    try:
        barrier.wait(timeout=timeout_seconds)
    except threading.BrokenBarrierError as exc:
        raise RuntimeError(f"process barrier broken while waiting for stage={stage_name}") from exc


def writer_worker(keys, shape, device_id, barrier, barrier_timeout_seconds, error_queue):
    store = None
    try:
        store = init_store(device_id)
        wait_process_barrier(barrier, "clients_ready", barrier_timeout_seconds)

        _, write_buffers, sizes, _ = build_pattern_write_tensors(num_tensors=len(keys), shape=shape)
        gvas = store.batch_alloc(keys, sizes)
        if len(gvas) != len(keys):
            raise AssertionError(f"batch_alloc returned {len(gvas)} gvas, expect {len(keys)}")

        ret = store.batch_copy(gvas, write_buffers, sizes, L2G)
        if ret != 0:
            raise AssertionError(f"writer batch_copy failed, ret={ret}")
        sync_stream()

        wait_process_barrier(barrier, "writer_finished", barrier_timeout_seconds)
        wait_process_barrier(barrier, "reader_verified", barrier_timeout_seconds)
    except AssertionError:
        error_queue.put(f"[writer]\n{traceback.format_exc()}")
        raise
    finally:
        if store is not None:
            store.close()


def reader_worker(keys, shape, device_id, barrier, barrier_timeout_seconds, error_queue):
    store = None
    try:
        store = init_store(device_id)
        wait_process_barrier(barrier, "clients_ready", barrier_timeout_seconds)
        wait_process_barrier(barrier, "writer_finished", barrier_timeout_seconds)

        read_tensors, read_buffers, sizes = build_read_tensors(num_tensors=len(keys), shape=shape)
        infos = store.batch_get_key_info(keys, 1)
        if len(infos) != len(keys):
            raise AssertionError(f"batch_get_key_info returned {len(infos)} infos, expect {len(keys)}")

        queried_gvas = []
        for info in infos:
            gva_list = info.gva_list()
            if len(gva_list) != 1:
                raise AssertionError(f"expect a single gva for each key, got {gva_list}")
            queried_gvas.append(gva_list[0])

        ret = store.batch_copy(queried_gvas, read_buffers, sizes, G2L)
        if ret != 0:
            raise AssertionError(f"reader batch_copy failed, ret={ret}")
        sync_stream()

        expected_sums = [idx + 1 for idx in range(len(keys))]
        numel = torch.Size(shape).numel()
        expected_sums = [value * numel for value in expected_sums]
        read_sums = [tensor.sum().item() for tensor in read_tensors]
        if read_sums != expected_sums:
            raise AssertionError(f"read_sums={read_sums} mismatch expected_sums={expected_sums}")

        wait_process_barrier(barrier, "reader_verified", barrier_timeout_seconds)
    except AssertionError:
        error_queue.put(f"[reader]\n{traceback.format_exc()}")
        raise
    finally:
        if store is not None:
            store.close()


class TestExample(unittest.TestCase):
    START_METHOD = "spawn"
    SINGLE_CLIENT_DEVICE_ID = 3
    WRITER_DEVICE_ID = 3
    READER_DEVICE_ID = 4
    META_BOOTSTRAP_SECONDS = 3
    BARRIER_TIMEOUT_SECONDS = 30
    PROCESS_JOIN_TIMEOUT_SECONDS = 90
    DEFAULT_SHAPE = (61, 10, 1024)

    @classmethod
    def setUpClass(cls):
        cls._mp_ctx = multiprocessing.get_context(cls.START_METHOD)
        cls._meta_process = cls._mp_ctx.Process(target=start_meta_service)
        cls._meta_process.start()
        print(f"MetaService started, pid={cls._meta_process.pid}")
        time.sleep(cls.META_BOOTSTRAP_SECONDS)

    @classmethod
    def tearDownClass(cls):
        proc = getattr(cls, "_meta_process", None)
        if proc is None:
            return

        print(f"Stopping MetaService, pid={proc.pid}")
        if proc.is_alive():
            proc.terminate()
            proc.join(timeout=2)
            if proc.is_alive():
                proc.kill()
                proc.join()

    def _set_device(self, device_id):
        acl.init()
        ret = acl.rt.set_device(device_id)
        self.assertEqual(ret, 0)

    def _sync_stream(self):
        torch_npu.npu.current_stream().synchronize()

    def _init_store(self, device_id):
        self._set_device(device_id)
        store = DistributedObjectStore()
        res = store.init(device_id)
        self.assertEqual(res, 0)
        return store

    def _build_write_tensors(self, num_tensors=4, shape=None):
        shape = shape or self.DEFAULT_SHAPE
        write_tensors = []
        buffers = []
        sizes = []
        for _ in range(num_tensors):
            tensor = torch.randint(
                low=0,
                high=256,
                size=shape,
                dtype=torch.uint8,
                device=torch.device("npu"),
            )
            write_tensors.append(tensor)
            buffers.append(tensor.data_ptr())
            sizes.append(tensor.element_size() * tensor.nelement())
        self._sync_stream()
        return write_tensors, buffers, sizes

    def _build_read_tensors(self, num_tensors=4, shape=None):
        shape = shape or self.DEFAULT_SHAPE
        read_tensors = []
        buffers = []
        for _ in range(num_tensors):
            tensor = torch.empty(
                size=shape,
                dtype=torch.uint8,
                device=torch.device("npu"),
            )
            read_tensors.append(tensor)
            buffers.append(tensor.data_ptr())
        return read_tensors, buffers

    def _build_keys(self, prefix, num_keys=4):
        suffix = uuid.uuid4().hex[:8]
        return [f"{prefix}_{suffix}_{idx}" for idx in range(num_keys)]

    def test_1_single_client(self):
        print("------------------------------ start single client ------------------------------")
        store = self._init_store(self.SINGLE_CLIENT_DEVICE_ID)
        try:
            keys = self._build_keys("single_client_key")
            write_tensors, buffers, sizes = self._build_write_tensors(num_tensors=len(keys))
            read_tensors, read_buffers = self._build_read_tensors(num_tensors=len(keys))
            read2_tensors, read2_buffers = self._build_read_tensors(num_tensors=len(keys))

            gvas = store.batch_alloc(keys, sizes)
            self.assertEqual(len(gvas), len(keys))

            ret = store.batch_copy(gvas, buffers, sizes, L2G)
            self.assertEqual(ret, 0)

            infos = store.batch_get_key_info(keys, 1)
            self.assertEqual(len(infos), len(keys))

            ret = store.batch_copy(gvas, read_buffers, sizes, G2L)
            self.assertEqual(ret, 0)
            self._sync_stream()

            for read_tensor, write_tensor in zip(read_tensors, write_tensors):
                self.assertEqual(read_tensor.sum().item(), write_tensor.sum().item())

            ret = store.batch_get_into(keys, read2_buffers, sizes, G2L)
            self.assertEqual(ret, [0] * len(keys))
            self._sync_stream()

            for read_tensor, write_tensor in zip(read2_tensors, write_tensors):
                self.assertEqual(read_tensor.sum().item(), write_tensor.sum().item())
        finally:
            store.close()

        print("------------------------------ over single client -------------------------------")

    def test_2_two_clients_with_barrier(self):
        print("------------------------------ start two client processes ------------------------------")
        keys = self._build_keys("two_client_key")
        ctx = multiprocessing.get_context(self.START_METHOD)
        barrier = ctx.Barrier(2)
        error_queue = ctx.Queue()

        processes = [
            ctx.Process(
                target=writer_worker,
                name="writer-client",
                args=(
                    keys,
                    self.DEFAULT_SHAPE,
                    self.WRITER_DEVICE_ID,
                    barrier,
                    self.BARRIER_TIMEOUT_SECONDS,
                    error_queue,
                ),
            ),
            ctx.Process(
                target=reader_worker,
                name="reader-client",
                args=(
                    keys,
                    self.DEFAULT_SHAPE,
                    self.READER_DEVICE_ID,
                    barrier,
                    self.BARRIER_TIMEOUT_SECONDS,
                    error_queue,
                ),
            ),
        ]
        try:
            for process in processes:
                process.start()

            for process in processes:
                process.join(timeout=self.PROCESS_JOIN_TIMEOUT_SECONDS)

            timed_out_processes = [process.name for process in processes if process.is_alive()]
            if timed_out_processes:
                for process in processes:
                    if process.is_alive():
                        process.terminate()
                for process in processes:
                    process.join(timeout=5)
                self.fail(f"processes did not finish in time: {timed_out_processes}")

            errors = []
            while True:
                try:
                    errors.append(error_queue.get_nowait())
                except queue.Empty:
                    break

            if errors:
                self.fail("\n".join(errors))
        finally:
            for process in processes:
                if process.is_alive():
                    process.terminate()
                    process.join(timeout=5)
                    if process.is_alive():
                        process.kill()
                        process.join(timeout=5)

        print("------------------------------ over two client processes -------------------------------")


if __name__ == "__main__":
    unittest.main()
