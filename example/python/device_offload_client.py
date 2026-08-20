#!/usr/bin/env python3
# coding=utf-8
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
# MemCache_Hybrid is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

"""Device 侧 Host DRAM 卸载验证进程。

MemCache 配置由 MMC_LOCAL_CONFIG_PATH 指向的配置文件加载。进程绑定 --dev-id
指定的 NPU，初始化 LocalService 后依次执行：

  batch_put_from_layers -> batch_get_key_info -> sparse/scatter copy -> 数据校验。

Device EID 不使用 USE_LOCAL_EID 覆盖，MemFabric 会根据设备信息从 RootInfo 自动读取。
Device 配置应使用 host_device_urma，保持与 Host 相同的 world_size/max DRAM/HBM
布局，并将本地 dram.size、hbm.size 都设为 0。

用法：

  export MMC_LOCAL_CONFIG_PATH=/path/to/mmc-device.conf
  python3 device_offload_client.py --copy_mode scatter --peer_rank 0 --dev-id 0
"""

import argparse
import os
import time

MEDIA_DRAM = 1
LAYER_COUNT = 2
TOKENS_PER_BLOCK = 128
CKV_WIDTH = 512
KPE_WIDTH = 64
CKV_TOKEN_BYTES = CKV_WIDTH * 2
KPE_TOKEN_BYTES = KPE_WIDTH * 2
CKV_LAYER_BYTES = TOKENS_PER_BLOCK * CKV_TOKEN_BYTES
LAYER_STRIDE_BYTES = TOKENS_PER_BLOCK * (CKV_TOKEN_BYTES + KPE_TOKEN_BYTES)
OBJECT_BYTES = LAYER_COUNT * LAYER_STRIDE_BYTES
SENTINEL = -1.0
POOL_READY_WAIT_SECONDS = 10


def _parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--copy_mode", "--copy-mode", dest="copy_mode", choices=("scatter", "sparse"), default="scatter"
    )
    parser.add_argument("--peer_rank", "--peer-rank", dest="peer_rank", type=int, default=0,
                        help="提供 Host DRAM 池的 peer rank，默认 0")
    parser.add_argument("--dev-id", type=int, default=0, help="NPU runtime device id，默认 0")
    return parser.parse_args()


def _configure_environment():
    config_path = os.getenv("MMC_LOCAL_CONFIG_PATH")
    if not config_path:
        raise RuntimeError("environment variable MMC_LOCAL_CONFIG_PATH is required")
    if not os.path.isfile(config_path):
        raise FileNotFoundError(f"MMC_LOCAL_CONFIG_PATH does not exist: {config_path}")
    # 不允许继承外部 EID 覆盖，DeviceUrmaTransportManager 将从 RootInfo 读取设备 EID。
    os.environ.pop("USE_LOCAL_EID", None)
    os.environ.pop("HCOMM_HOST_ONLY", None)
    os.environ.pop("MF_LOCAL_DRAM_VALIDATION_ROLE", None)
    os.environ.pop("MF_HOST_URMA_EID", None)
    return config_path


def _set_runtime_device(torch, device_id):
    device = torch.device(f"npu:{device_id}")
    torch.npu.set_device(device)
    if torch.npu.current_device() != device_id:
        raise RuntimeError(f"failed to select NPU device {device_id}")
    return device


def _make_source_layers(torch, device):
    source_ckv = []
    source_kpe = []
    source_layers = []
    for layer_index in range(LAYER_COUNT):
        ckv = (torch.arange(TOKENS_PER_BLOCK * CKV_WIDTH) % 13 + layer_index * 16).to(torch.bfloat16)
        kpe = (torch.arange(TOKENS_PER_BLOCK * KPE_WIDTH) % 7 + layer_index * 16 + 8).to(torch.bfloat16)
        ckv = ckv.reshape(TOKENS_PER_BLOCK, CKV_WIDTH).to(device)
        kpe = kpe.reshape(TOKENS_PER_BLOCK, KPE_WIDTH).to(device)
        source_ckv.append(ckv)
        source_kpe.append(kpe)
        source_layers.append(torch.cat((ckv.flatten(), kpe.flatten())))
    return source_ckv, source_kpe, source_layers


def _put_to_peer(store, key, source_layers, peer_rank, l2g, replicate_config_cls):
    replicate_config = replicate_config_cls()
    replicate_config.replicaNum = 1
    replicate_config.preferredLocalServiceIDs = [peer_rank]
    results = store.batch_put_from_layers(
        [key],
        [[layer.data_ptr() for layer in source_layers]],
        [[LAYER_STRIDE_BYTES] * LAYER_COUNT],
        l2g,
        replicate_config,
    )
    if results != [0]:
        raise AssertionError(f"batch_put_from_layers failed: results={results}")


def _query_peer_gva(store, key, peer_rank):
    infos = store.batch_get_key_info([key], flag=0)
    if len(infos) != 1:
        raise AssertionError(f"batch_get_key_info returned {len(infos)} entries")
    info = infos[0]
    if info.size() != OBJECT_BYTES:
        raise AssertionError(f"unexpected object size: {info.size()}")
    if info.loc_list() != [peer_rank] or info.type_list() != [MEDIA_DRAM]:
        raise AssertionError(f"unexpected location: ranks={info.loc_list()}, media={info.type_list()}")
    if len(info.gva_list()) != 1 or info.gva_list()[0] == 0:
        raise AssertionError(f"invalid peer GVA: {info.gva_list()}")
    return info.gva_list()[0]


def _sparse_copy_and_verify(torch, offload, device, peer_gva, source_ckv, source_kpe):
    destinations = [
        torch.full((CKV_WIDTH,), SENTINEL, dtype=torch.bfloat16, device=device),
        torch.full((KPE_WIDTH,), SENTINEL, dtype=torch.bfloat16, device=device),
    ]
    source_addresses = [
        peer_gva + 7 * CKV_TOKEN_BYTES,
        peer_gva + LAYER_STRIDE_BYTES + CKV_LAYER_BYTES + 11 * KPE_TOKEN_BYTES,
    ]
    src_ptrs = torch.tensor(source_addresses, dtype=torch.int64, device=device)
    dst_ptrs = torch.tensor([tensor.data_ptr() for tensor in destinations], dtype=torch.int64, device=device)
    len_ptrs = torch.tensor([CKV_TOKEN_BYTES, KPE_TOKEN_BYTES], dtype=torch.int64, device=device)
    ret = offload.sparse_copy_urma(src_ptrs, dst_ptrs, len_ptrs, len(destinations), device)
    if ret != 0:
        raise AssertionError(f"sparse_copy_urma failed: ret={ret}")
    torch.npu.synchronize()
    if not torch.equal(destinations[0], source_ckv[0][7]):
        raise AssertionError("sparse_copy_urma CKV data mismatch")
    if not torch.equal(destinations[1], source_kpe[1][11]):
        raise AssertionError("sparse_copy_urma KPE data mismatch")


def _scatter_copy_layer(torch, offload, hbm_kpe, hbm_ckv, tables, plan):
    hbm_block_table, dram_block_table, offload_slots = tables
    layer_id, src_tokens, dst_tokens = plan
    src_token_ids = torch.tensor(src_tokens, dtype=torch.int32, device=hbm_kpe.device)
    dst_slots = torch.tensor(dst_tokens, dtype=torch.int32, device=hbm_kpe.device)
    copy_counts = torch.tensor([len(src_tokens)], dtype=torch.int32, device=hbm_kpe.device)
    offload.npu_kvcache_scatter_copy(
        hbm_kpe, hbm_ckv, None, None,
        hbm_block_table, dram_block_table,
        offload_slots, src_token_ids, dst_slots, copy_counts,
        ready_flag=None, layer_id=layer_id,
    )


def _scatter_copy_and_verify(torch, offload, device, peer_gva, source_ckv, source_kpe):
    hbm_ckv_buffer = torch.full(
        (1, TOKENS_PER_BLOCK * CKV_WIDTH), SENTINEL, dtype=torch.bfloat16, device=device
    )
    hbm_kpe_buffer = torch.full(
        (1, TOKENS_PER_BLOCK * KPE_WIDTH), SENTINEL, dtype=torch.bfloat16, device=device
    )
    tables = (
        torch.tensor([[0]], dtype=torch.int32, device=device),
        torch.tensor([[peer_gva]], dtype=torch.int64, device=device),
        torch.tensor([0], dtype=torch.int32, device=device),
    )
    plans = [(0, [7, 42], [3, 6]), (1, [11], [5])]
    for plan in plans:
        _scatter_copy_layer(torch, offload, hbm_kpe_buffer, hbm_ckv_buffer, tables, plan)
    torch.npu.synchronize()
    hbm_ckv = hbm_ckv_buffer.view(TOKENS_PER_BLOCK, CKV_WIDTH)
    hbm_kpe = hbm_kpe_buffer.view(TOKENS_PER_BLOCK, KPE_WIDTH)
    for layer_id, src_token, dst_token in ((0, 7, 3), (0, 42, 6), (1, 11, 5)):
        if not torch.equal(hbm_ckv[dst_token], source_ckv[layer_id][src_token]):
            raise AssertionError(f"scatter_copy layer{layer_id} token{src_token} CKV mismatch")
        if not torch.equal(hbm_kpe[dst_token], source_kpe[layer_id][src_token]):
            raise AssertionError(f"scatter_copy layer{layer_id} token{src_token} KPE mismatch")


def _run(store, args, torch, offload, l2g, replicate_config_cls):
    device = torch.device(f"npu:{args.dev_id}")
    source_ckv, source_kpe, source_layers = _make_source_layers(torch, device)
    torch.npu.synchronize()
    key = f"device-offload-{os.getpid()}-{time.time_ns()}"
    key_created = False
    lease_added = False
    copy_passed = False
    try:
        _put_to_peer(store, key, source_layers, args.peer_rank, l2g, replicate_config_cls)
        key_created = True
        peer_gva = _query_peer_gva(store, key, args.peer_rank)
        if store.batch_add_lease([key]) != [0]:
            raise AssertionError("batch_add_lease failed")
        lease_added = True
        if args.copy_mode == "sparse":
            _sparse_copy_and_verify(torch, offload, device, peer_gva, source_ckv, source_kpe)
        else:
            _scatter_copy_and_verify(torch, offload, device, peer_gva, source_ckv, source_kpe)
        copy_passed = True
    finally:
        cleanup_errors = []
        if lease_added:
            try:
                lease_ret = store.batch_remove_lease([key])
                if lease_ret != 0:
                    cleanup_errors.append(f"batch_remove_lease ret={lease_ret}")
            except Exception as error:  # pylint: disable=broad-exception-caught
                cleanup_errors.append(f"batch_remove_lease raised {error}")
        if key_created:
            try:
                remove_results = store.remove_batch([key])
                if remove_results != [0]:
                    cleanup_errors.append(f"remove_batch results={remove_results}")
            except Exception as error:  # pylint: disable=broad-exception-caught
                cleanup_errors.append(f"remove_batch raised {error}")
        if copy_passed and cleanup_errors:
            raise RuntimeError("; ".join(cleanup_errors))
        if cleanup_errors:
            print(f"cleanup warning: {'; '.join(cleanup_errors)}", flush=True)
    local_rank = store.get_local_service_id()
    print(
        f"DEVICE PASS: rank={local_rank} peer_rank={args.peer_rank} "
        f"copy_mode={args.copy_mode} peer_gva=0x{peer_gva:x}",
        flush=True,
    )


def main():
    args = _parse_args()
    if args.dev_id < 0 or args.peer_rank < 0:
        raise ValueError("dev-id and peer_rank must be non-negative")
    config_path = _configure_environment()

    import torch
    import torch_npu  # noqa: F401
    from memcache_hybrid import DistributedObjectStore, L2G, ReplicateConfig
    from memfabric_hybrid import offload

    _set_runtime_device(torch, args.dev_id)
    store = DistributedObjectStore()
    initialized = False
    try:
        init_ret = store.init(args.dev_id)
        if init_ret != 0:
            raise RuntimeError(f"store.init failed: ret={init_ret}")
        initialized = True
        print(f"DEVICE_READY: config={config_path} dev_id={args.dev_id}", flush=True)
        # world join 完成不代表 peer 池已经注册到 MetaService，等待已验证的注册窗口。
        time.sleep(POOL_READY_WAIT_SECONDS)
        _run(store, args, torch, offload, L2G, ReplicateConfig)
    finally:
        if initialized:
            store.close()


if __name__ == "__main__":
    main()
