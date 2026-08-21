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

  put/alloc 数据准备 -> batch_get_key_info -> sparse/scatter copy -> 数据校验。

Device EID 不使用 USE_LOCAL_EID 覆盖，MemFabric 会根据设备信息从 RootInfo 自动读取。
Device 配置应使用 host_device_urma，保持与 Host 相同的 world_size/max DRAM/HBM
布局，并将本地 dram.size、hbm.size 都设为 0。

用法：

  export MMC_LOCAL_CONFIG_PATH=/path/to/mmc-device.conf
  python3 device_offload_client.py --prepare-mode put --copy-mode scatter --peer-rank 0 --dev-id 0 \
    --copy-rounds 100 --copy-tokens 128
"""

import argparse
import math
import os
import time
import zlib

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
COPY_WARMUP_ROUNDS = 3
DEFAULT_COPY_ROUNDS = 100
DEFAULT_COPY_TOKENS = TOKENS_PER_BLOCK


def _parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--copy_mode", "--copy-mode", dest="copy_mode", choices=("scatter", "sparse"), default="scatter"
    )
    parser.add_argument(
        "--prepare-mode",
        choices=("put", "alloc"),
        default="put",
        help="远端数据准备方式：put=指定 peer；alloc=按 DRAM 介质随机选 rank 后写入",
    )
    parser.add_argument("--peer_rank", "--peer-rank", dest="peer_rank", type=int, default=0,
                        help="提供 Host DRAM 池的 peer rank，默认 0")
    parser.add_argument("--dev-id", type=int, default=0, help="NPU runtime device id，默认 0")
    parser.add_argument("--copy-rounds", type=int, default=DEFAULT_COPY_ROUNDS,
                        help=f"计入带宽和延迟统计的 copy 轮次，默认 {DEFAULT_COPY_ROUNDS}")
    parser.add_argument("--copy-tokens", type=int, default=DEFAULT_COPY_TOKENS,
                        help=f"每轮复制的 CKV+KPE token 数，范围 1-{TOKENS_PER_BLOCK}")
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


def _checksum_tensors(torch, tensors):
    checksum = 0
    total_bytes = 0
    for tensor in tensors:
        data = tensor.detach().contiguous().cpu().view(torch.uint8).numpy().tobytes()
        checksum = zlib.crc32(data, checksum)
        total_bytes += len(data)
    return checksum, total_bytes


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


def _alloc_to_peer(store, key, source_layers, l2g):
    gvas = store.batch_alloc([key], [OBJECT_BYTES], media=MEDIA_DRAM)
    if len(gvas) != 1 or gvas[0] == 0:
        raise AssertionError(f"batch_alloc failed: gvas={gvas}")
    copy_ret = store.batch_copy_layers(
        gva_ptrs=gvas,
        buffer_ptrs=[[layer.data_ptr() for layer in source_layers]],
        sizes=[[LAYER_STRIDE_BYTES] * LAYER_COUNT],
        direct=l2g,
    )
    finish_results = store.batch_write_finish(keys=[key], res=[copy_ret])
    if copy_ret != 0:
        raise AssertionError(f"batch_copy_layers failed: ret={copy_ret}, finish_results={finish_results}")
    if finish_results != [0]:
        raise AssertionError(f"batch_write_finish failed: results={finish_results}")


def _prepare_peer_data(store, args, key, source_layers, l2g, replicate_config_cls):
    if args.prepare_mode == "alloc":
        _alloc_to_peer(store, key, source_layers, l2g)
        return
    _put_to_peer(store, key, source_layers, args.peer_rank, l2g, replicate_config_cls)


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


def _measure_copy(torch, copy_once, rounds):
    for _ in range(COPY_WARMUP_ROUNDS):
        copy_once()
    torch.npu.synchronize()

    start_ns = time.perf_counter_ns()
    for _ in range(rounds):
        copy_once()
    torch.npu.synchronize()
    elapsed_ns = time.perf_counter_ns() - start_ns

    latency_ms = []
    for _ in range(rounds):
        start_event = torch.npu.Event(enable_timing=True)
        end_event = torch.npu.Event(enable_timing=True)
        torch.npu.synchronize()
        start_event.record()
        copy_once()
        end_event.record()
        torch.npu.synchronize()
        latency_ms.append(start_event.elapsed_time(end_event))
    return elapsed_ns, latency_ms


def _percentile(values, percentile):
    ordered = sorted(values)
    index = max(0, math.ceil(percentile * len(ordered)) - 1)
    return ordered[index]


def _make_copy_stats(mode, rounds, token_count, copied_bytes, elapsed_ns, latency_ms, checksum, expected_checksum):
    bytes_per_round = copied_bytes
    total_bytes = bytes_per_round * rounds
    elapsed_ms = elapsed_ns / 1_000_000
    bandwidth_gbps = total_bytes / (elapsed_ns / 1_000_000_000) / 1_000_000_000
    return {
        "mode": mode,
        "rounds": rounds,
        "tokens": token_count,
        "bytes_per_round": bytes_per_round,
        "total_bytes": total_bytes,
        "elapsed_ms": elapsed_ms,
        "bandwidth_gbps": bandwidth_gbps,
        "latency_min_ms": min(latency_ms),
        "latency_avg_ms": sum(latency_ms) / len(latency_ms),
        "latency_p95_ms": _percentile(latency_ms, 0.95),
        "latency_p99_ms": _percentile(latency_ms, 0.99),
        "latency_p9999_ms": _percentile(latency_ms, 0.9999),
        "latency_max_ms": max(latency_ms),
        "checksum": f"0x{checksum:08x}",
        "expected": f"0x{expected_checksum:08x}",
    }


def _print_copy_stats(stats):
    headers = ("Mode", "Rounds", "Tokens/Round", "Bytes/Round", "Total Bytes", "Elapsed(ms)", "GB/s",
               "Checksum", "Expected")
    values = (
        stats["mode"], str(stats["rounds"]), str(stats["tokens"]), str(stats["bytes_per_round"]),
        str(stats["total_bytes"]), f"{stats['elapsed_ms']:.3f}", f"{stats['bandwidth_gbps']:.3f}",
        stats["checksum"], stats["expected"],
    )
    widths = [max(len(header), len(value)) for header, value in zip(headers, values)]
    border = "+-" + "-+-".join("-" * width for width in widths) + "-+"
    print("COPY PERFORMANCE", flush=True)
    print(border, flush=True)
    print("| " + " | ".join(header.ljust(width) for header, width in zip(headers, widths)) + " |", flush=True)
    print(border, flush=True)
    print("| " + " | ".join(value.ljust(width) for value, width in zip(values, widths)) + " |", flush=True)
    print(border, flush=True)

    latency_headers = ("Samples", "Min(ms)", "Avg(ms)", "P95(ms)", "P99(ms)", "P99.99(ms)", "Max(ms)")
    latency_values = (
        str(stats["rounds"]), f"{stats['latency_min_ms']:.3f}", f"{stats['latency_avg_ms']:.3f}",
        f"{stats['latency_p95_ms']:.3f}", f"{stats['latency_p99_ms']:.3f}",
        f"{stats['latency_p9999_ms']:.3f}", f"{stats['latency_max_ms']:.3f}",
    )
    latency_widths = [max(len(header), len(value)) for header, value in zip(latency_headers, latency_values)]
    latency_border = "+-" + "-+-".join("-" * width for width in latency_widths) + "-+"
    print("COPY LATENCY", flush=True)
    print(latency_border, flush=True)
    print("| " + " | ".join(header.ljust(width) for header, width in zip(latency_headers, latency_widths)) + " |",
          flush=True)
    print(latency_border, flush=True)
    print("| " + " | ".join(value.ljust(width) for value, width in zip(latency_values, latency_widths)) + " |",
          flush=True)
    print(latency_border, flush=True)


def _sparse_copy_and_verify(torch, offload, device, peer_gva, source_ckv, source_kpe, token_count, rounds):
    destination_ckv = torch.full((token_count, CKV_WIDTH), SENTINEL, dtype=torch.bfloat16, device=device)
    destination_kpe = torch.full((token_count, KPE_WIDTH), SENTINEL, dtype=torch.bfloat16, device=device)
    source_addresses = []
    destination_addresses = []
    lengths = []
    for token_id in range(token_count):
        source_addresses.extend((peer_gva + token_id * CKV_TOKEN_BYTES,
                                 peer_gva + CKV_LAYER_BYTES + token_id * KPE_TOKEN_BYTES))
        destination_addresses.extend((destination_ckv[token_id].data_ptr(), destination_kpe[token_id].data_ptr()))
        lengths.extend((CKV_TOKEN_BYTES, KPE_TOKEN_BYTES))
    src_ptrs = torch.tensor(source_addresses, dtype=torch.int64, device=device)
    dst_ptrs = torch.tensor(destination_addresses, dtype=torch.int64, device=device)
    len_ptrs = torch.tensor(lengths, dtype=torch.int64, device=device)

    def copy_once():
        ret = offload.sparse_copy_urma(src_ptrs, dst_ptrs, len_ptrs, len(source_addresses), device)
        if ret != 0:
            raise AssertionError(f"sparse_copy_urma failed: ret={ret}")

    elapsed_ns, latency_ms = _measure_copy(torch, copy_once, rounds)
    if not torch.equal(destination_ckv, source_ckv[0][:token_count]):
        raise AssertionError("sparse_copy_urma CKV data mismatch")
    if not torch.equal(destination_kpe, source_kpe[0][:token_count]):
        raise AssertionError("sparse_copy_urma KPE data mismatch")
    actual_checksum, copied_bytes = _checksum_tensors(torch, [destination_ckv, destination_kpe])
    expected_checksum, _ = _checksum_tensors(torch, [source_ckv[0][:token_count], source_kpe[0][:token_count]])
    return _make_copy_stats("sparse", rounds, token_count, copied_bytes, elapsed_ns, latency_ms, actual_checksum,
                            expected_checksum)


def _scatter_copy_once(offload, hbm_kpe, hbm_ckv, tables, copy_params):
    hbm_block_table, dram_block_table, offload_slots = tables
    src_token_ids, dst_slots, copy_counts = copy_params
    offload.npu_kvcache_scatter_copy(
        hbm_kpe, hbm_ckv, None, None,
        hbm_block_table, dram_block_table,
        offload_slots, src_token_ids, dst_slots, copy_counts,
        ready_flag=None, layer_id=0,
    )


def _scatter_copy_and_verify(torch, offload, device, peer_gva, source_ckv, source_kpe, token_count, rounds):
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
    token_ids = list(range(token_count))
    copy_params = (
        torch.tensor(token_ids, dtype=torch.int32, device=device),
        torch.tensor(token_ids, dtype=torch.int32, device=device),
        torch.tensor([token_count], dtype=torch.int32, device=device),
    )

    def copy_once():
        _scatter_copy_once(offload, hbm_kpe_buffer, hbm_ckv_buffer, tables, copy_params)

    elapsed_ns, latency_ms = _measure_copy(torch, copy_once, rounds)
    hbm_ckv = hbm_ckv_buffer.view(TOKENS_PER_BLOCK, CKV_WIDTH)
    hbm_kpe = hbm_kpe_buffer.view(TOKENS_PER_BLOCK, KPE_WIDTH)
    copied_tensors = [hbm_ckv[:token_count], hbm_kpe[:token_count]]
    expected_tensors = [source_ckv[0][:token_count], source_kpe[0][:token_count]]
    if not torch.equal(copied_tensors[0], expected_tensors[0]):
        raise AssertionError("scatter_copy CKV data mismatch")
    if not torch.equal(copied_tensors[1], expected_tensors[1]):
        raise AssertionError("scatter_copy KPE data mismatch")
    actual_checksum, copied_bytes = _checksum_tensors(torch, copied_tensors)
    expected_checksum, _ = _checksum_tensors(torch, expected_tensors)
    return _make_copy_stats("scatter", rounds, token_count, copied_bytes, elapsed_ns, latency_ms, actual_checksum,
                            expected_checksum)


def _run(store, args, torch, offload, l2g, replicate_config_cls):
    device = torch.device(f"npu:{args.dev_id}")
    source_ckv, source_kpe, source_layers = _make_source_layers(torch, device)
    torch.npu.synchronize()
    key = f"device-offload-{os.getpid()}-{time.time_ns()}"
    key_created = False
    lease_added = False
    copy_passed = False
    try:
        source_checksum, source_bytes = _checksum_tensors(torch, source_layers)
        print(
            f"PUT_BEFORE: mode={args.prepare_mode} key={key} "
            f"checksum=0x{source_checksum:08x} bytes={source_bytes}",
            flush=True,
        )
        _prepare_peer_data(store, args, key, source_layers, l2g, replicate_config_cls)
        key_created = True
        peer_gva = _query_peer_gva(store, key, args.peer_rank)
        if store.batch_add_lease([key]) != [0]:
            raise AssertionError("batch_add_lease failed")
        lease_added = True
        if args.copy_mode == "sparse":
            copy_stats = _sparse_copy_and_verify(
                torch, offload, device, peer_gva, source_ckv, source_kpe, args.copy_tokens, args.copy_rounds
            )
        else:
            copy_stats = _scatter_copy_and_verify(
                torch, offload, device, peer_gva, source_ckv, source_kpe, args.copy_tokens, args.copy_rounds
            )
        _print_copy_stats(copy_stats)
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
        f"prepare_mode={args.prepare_mode} copy_mode={args.copy_mode} peer_gva=0x{peer_gva:x}",
        flush=True,
    )


def main():
    args = _parse_args()
    if args.dev_id < 0 or args.peer_rank < 0 or args.copy_rounds <= 0:
        raise ValueError("dev-id and peer_rank must be non-negative; copy-rounds must be positive")
    if args.copy_tokens <= 0 or args.copy_tokens > TOKENS_PER_BLOCK:
        raise ValueError(f"copy-tokens must be in range 1-{TOKENS_PER_BLOCK}")
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
