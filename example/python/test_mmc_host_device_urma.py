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

"""Validate MemCache Host DRAM to Device HBM URMA with two processes.

这个用例用两个进程验证一条"Host DRAM 卸载到 Device HBM"的完整链路：

  1. Host 进程（rank 0，纯 CPU）创建一个 1 GiB 的 Host DRAM 池并注册到 MetaService。
  2. Device 进程（rank 1，绑定一张 NPU）在本地 HBM 上构造 KV cache 数据（CKV/KPE layer）。
  3. Device 调用 MemCache batch_put_from_layers(L2G, preferredHostRank)：
       - L2G 表示源数据在 Device 本地 HBM（local HBM -> global）
       - preferredHostRank 指定数据落到 Host rank 的 DRAM 池
       - MetaService 按目标 rank 的可用池（DRAM）分配 GVA
  4. Device 调用 batch_get_key_info 拿到对象大小、所在 rank、介质类型和 GVA。
  5. Device 在 GVA 上叠加一个偏移，把源地址/HBM 目标地址/长度交给 MemFabric
     sparse_copy_urma；AICPU 从 Host DRAM 复制片段到 HBM。
  6. 比较复制回来的 HBM 数据与原始对象对应切片。

  也可以把第 5 步换成 MemFabric 的 npu_kvcache_scatter_copy：不再直接给地址列表，
  而是给 [offload slot, src token id, dst token id] 三元组和两级 block table，
  由 kernel 内部换算 Host DRAM 的源 GVA 与 HBM 的目标 token 地址后按 token 粒度
  分散复制（本文件默认即使用 scatter_copy 路径）。
"""

import argparse
import os
import time

# ------------------------- 常量定义 -------------------------
# world_size=2：rank 0 = Host（DRAM 池），rank 1 = Device（HBM 池）
WORLD_SIZE = 2
# 介质类型：MEDIA_HBM=0, MEDIA_DRAM=1（与 C++ MediaType 枚举一致）
MEDIA_DRAM = 1
# 每个对象的 layer 数量（KV cache 按 layer 分片存储）
LAYER_COUNT = 2
# 每个 token 的尺寸（KV cache 的 block 结构）
TOKENS_PER_BLOCK = 128
CKV_WIDTH = 512
KPE_WIDTH = 64
# CKV/KPE 每个 token 的字节数（bfloat16 = 2 字节）
CKV_TOKEN_BYTES = CKV_WIDTH * 2
KPE_TOKEN_BYTES = KPE_WIDTH * 2
# 一个 block 内 CKV 部分的字节数
CKV_LAYER_BYTES = TOKENS_PER_BLOCK * CKV_TOKEN_BYTES
# 一层 KV cache 的字节数 = block 内 CKV + KPE
LAYER_STRIDE_BYTES = TOKENS_PER_BLOCK * (CKV_TOKEN_BYTES + KPE_TOKEN_BYTES)
# 单个对象的完整字节数 = layer 数 x 每层字节数
OBJECT_BYTES = LAYER_COUNT * LAYER_STRIDE_BYTES
# 目标 HBM tensor 的填充值（用于初始化和校验）
SENTINEL = -1.0
# 两种角色
ROLE_HOST = "host"
ROLE_DEVICE = "device"


def _validate_eid(value, name):
    """校验 URMA EID：必须是 32 位十六进制且非全零，返回小写形式。"""
    if value is None or len(value) != 32 or any(character not in "0123456789abcdefABCDEF" for character in value):
        raise ValueError(f"{name} must be exactly 32 hexadecimal characters")
    if int(value, 16) == 0:
        raise ValueError(f"{name} must not be all zero")
    return value.lower()


def _configure_environment(args):
    """按角色设置 MemFabric/HCOMM 环境变量。

    Host 角色：开启 HCOMM host-only、本地 DRAM validation、禁用未使用的 RDMA swap，
    并写入 Host URMA EID。
    Device 角色：清除 host-only / host validation 变量，写入 Device URMA EID（USE_LOCAL_EID）。
    """
    if args.role == ROLE_HOST:
        host_eid = _validate_eid(args.host_eid or os.getenv("MF_HOST_URMA_EID"), "Host EID")
        os.environ["HCOMM_HOST_ONLY"] = "1"
        os.environ["MF_LOCAL_DRAM_VALIDATION_ROLE"] = "host"
        os.environ["MF_HYBM_RDMA_SWAP_SPACE_SIZE"] = "0"
        os.environ["MF_HOST_URMA_EID"] = host_eid
        return

    device_eid = _validate_eid(args.device_eid or os.getenv("USE_LOCAL_EID"), "Device EID")
    os.environ.pop("HCOMM_HOST_ONLY", None)
    os.environ.pop("MF_LOCAL_DRAM_VALIDATION_ROLE", None)
    os.environ["USE_LOCAL_EID"] = device_eid


def _resolve_physical_device(args):
    """把进程内逻辑 device id 映射为真实物理卡号，并校验与显式指定一致。

    若设置了 ASCEND_RT_VISIBLE_DEVICES，则其中第 device_id 项即物理卡号；
    否则物理卡号就等于 device_id。用于确保 URMA EID 对应的卡与 torch 使用的卡一致。
    """
    visible_devices = os.getenv("ASCEND_RT_VISIBLE_DEVICES")
    if visible_devices:
        devices = [item.strip() for item in visible_devices.split(",") if item.strip()]
        if args.device_id >= len(devices):
            raise ValueError(
                f"--device-id {args.device_id} is outside ASCEND_RT_VISIBLE_DEVICES={visible_devices}"
            )
        mapped_physical_device = int(devices[args.device_id])
    else:
        mapped_physical_device = args.device_id
    if args.physical_device_id is not None and args.physical_device_id != mapped_physical_device:
        raise ValueError(
            f"--physical-device-id {args.physical_device_id} does not match runtime mapping "
            f"{mapped_physical_device}"
        )
    args.physical_device_id = mapped_physical_device


def _set_runtime_device(device_id):
    """把当前线程的 torch/ACL runtime 切换到指定 NPU 逻辑设备。"""
    import torch
    import torch_npu  # noqa: F401

    torch.npu.set_device(torch.device(f"npu:{device_id}"))
    current_device = torch.npu.current_device()
    if current_device != device_id:
        raise RuntimeError(f"failed to select NPU device: requested={device_id}, current={current_device}")


def _make_local_config(role):
    """构造 MemCache LocalService 的本地配置。

    两种角色共享相同的 GVA 布局（max_dram/max_hbm 都为 1 GiB），
    但只按角色分配实际本地池：Host 只建 1 GiB DRAM 池，Device 不申请任何本地内存。
    Device 的源/目标 tensor 由 torch 直接分配在 HBM，不经过 MemCache 池。
    """
    from memcache_hybrid import LocalConfig

    config = LocalConfig()
    config.meta_service_url = os.getenv("MMC_META_SERVICE_URL", "tcp://127.0.0.1:5000")
    config.config_store_url = os.getenv("MMC_CONFIG_STORE_URL", "tcp://127.0.0.1:6000")
    config.hcom_url = os.getenv("MMC_HCOM_URL", "tcp://127.0.0.1:7000")
    config.protocol = "host_device_urma"
    config.world_size = WORLD_SIZE
    config.backend_id = f"e2e-host-device-urma-{role}-{os.getpid()}"
    config.max_dram_size = "1GB"
    config.max_hbm_size = "1GB"
    config.dram_size = "1GB" if role == ROLE_HOST else "0"
    config.hbm_size = "0"
    return config


def _make_source_layers(torch, device):
    """在 Device 本地 HBM 上构造 KV cache 源数据。

    每个 layer 由一块 CKV（形状 [TOKENS, CKV_WIDTH]）和一块 KPE（形状
    [TOKENS, KPE_WIDTH]）拼接成连续 buffer。返回值：
      - source_ckv / source_kpe：分层保留的 CKV/KPE 原始 tensor（用于校验）
      - source_layers：每层拼成的连续 buffer（data_ptr 作为 put 源地址）
    """
    source_ckv = []
    source_kpe = []
    source_layers = []
    for layer_index in range(LAYER_COUNT):
        # 用确定性公式生成数据，便于 Device 端复现校验
        ckv = (torch.arange(TOKENS_PER_BLOCK * CKV_WIDTH) % 13 + layer_index * 16).to(torch.bfloat16)
        kpe = (torch.arange(TOKENS_PER_BLOCK * KPE_WIDTH) % 7 + layer_index * 16 + 8).to(torch.bfloat16)
        ckv = ckv.reshape(TOKENS_PER_BLOCK, CKV_WIDTH).to(device)
        kpe = kpe.reshape(TOKENS_PER_BLOCK, KPE_WIDTH).to(device)
        source_ckv.append(ckv)
        source_kpe.append(kpe)
        # 一层 = 该 layer 的 CKV 展平 + KPE 展平，作为连续内存传给 put
        source_layers.append(torch.cat((ckv.flatten(), kpe.flatten())))
    return source_ckv, source_kpe, source_layers


def _put_to_host(store, key, source_layers, host_rank):
    """把 Device 本地 HBM 上的数据写入指定 Host rank 的 DRAM 池。

    - L2G：源数据在 Device 本地 HBM（local HBM -> global space）
    - preferredLocalServiceIDs=[host_rank]：目标落在该 Host rank 的池
    MetaService 会按目标 rank 实际注册的池（DRAM）分配 GVA，并完成 L2G 写入。
    """
    from memcache_hybrid import L2G, ReplicateConfig

    replicate_config = ReplicateConfig()
    replicate_config.replicaNum = 1
    replicate_config.preferredLocalServiceIDs = [host_rank]
    results = store.batch_put_from_layers(
        [key],
        [[layer.data_ptr() for layer in source_layers]],
        [[LAYER_STRIDE_BYTES] * LAYER_COUNT],
        L2G,
        replicate_config,
    )
    if results != [0]:
        raise AssertionError(f"batch_put_from_layers failed: results={results}")


def _query_host_gva(store, key, host_rank):
    """通过 batch_get_key_info 查询对象元数据并返回 Host DRAM 上的 GVA。

    校验对象大小、所在 rank 和介质类型，确认它确实落在了 Host rank 的 DRAM。
    """
    infos = store.batch_get_key_info([key], flag=0)
    if len(infos) != 1:
        raise AssertionError(f"batch_get_key_info returned {len(infos)} entries")
    info = infos[0]
    if info.size() != OBJECT_BYTES:
        raise AssertionError(f"unexpected object size: {info.size()}")
    if info.loc_list() != [host_rank] or info.type_list() != [MEDIA_DRAM]:
        raise AssertionError(f"unexpected location: ranks={info.loc_list()}, media={info.type_list()}")
    if len(info.gva_list()) != 1 or info.gva_list()[0] == 0:
        raise AssertionError(f"invalid Host GVA: {info.gva_list()}")
    return info.gva_list()[0]


def _sparse_copy_and_verify(torch, mf_acc_offload, device, host_gva, source_ckv, source_kpe):
    """调用 MemFabric sparse_copy_urma 从 Host DRAM GVA 读取片段到 HBM 并校验。

    在 Host GVA 上叠加两个偏移，分别指向对象内 CKV 和 KPE 的某个 token 位置，
    构造 [源GVA+偏移, 目标HBM地址, 长度] 三个 int64 tensor 传给 AICPU kernel，
    复制完成后把目标 HBM tensor 与原始 source_ckv/source_kpe 对应切片比较。
    """
    # 目标 HBM tensor：两个待填充的片段
    destinations = [
        torch.full((CKV_WIDTH,), SENTINEL, dtype=torch.bfloat16, device=device),
        torch.full((KPE_WIDTH,), SENTINEL, dtype=torch.bfloat16, device=device),
    ]
    # 源地址 = Host GVA + 偏移（验证 GVA 可直接参与地址运算）
    source_addresses = [
        host_gva + 7 * CKV_TOKEN_BYTES,
        host_gva + LAYER_STRIDE_BYTES + CKV_LAYER_BYTES + 11 * KPE_TOKEN_BYTES,
    ]
    src_ptrs = torch.tensor(source_addresses, dtype=torch.int64, device=device)
    dst_ptrs = torch.tensor([tensor.data_ptr() for tensor in destinations], dtype=torch.int64, device=device)
    len_ptrs = torch.tensor([CKV_TOKEN_BYTES, KPE_TOKEN_BYTES], dtype=torch.int64, device=device)
    ret = mf_acc_offload.sparse_copy_urma(src_ptrs, dst_ptrs, len_ptrs, len(destinations), device)
    if ret != 0:
        raise AssertionError(f"sparse_copy_urma failed: ret={ret}")
    torch.npu.synchronize()
    # 校验复制回来的数据与原始对象对应切片一致
    if not torch.equal(destinations[0], source_ckv[0][7]):
        raise AssertionError("sparse_copy_urma CKV data mismatch")
    if not torch.equal(destinations[1], source_kpe[1][11]):
        raise AssertionError("sparse_copy_urma KPE data mismatch")


def _scatter_copy_and_verify(torch, mf_acc_offload, device, host_gva, source_ckv, source_kpe):
    """调用 MemFabric npu_kvcache_scatter_copy 按 token 粒度从 Host DRAM 写回 HBM 并校验。

    scatter_copy 与 sparse_copy_urma 的区别：不直接给地址列表，而是给
    [offload slot, src token id, dst token id] 三元组 + 两级 block table，
    由 kernel 内部换算源 GVA 和目标 HBM 地址：
      - srcToken 的 block/offset 决定 DRAM 侧地址：blockGva = dramBlockTable[slot][srcBlock]，
        再叠加 layerId*每层字节 + offset*每 token 字节
      - dstToken 的 block/offset 决定 HBM 侧地址：physicalBlock = hbmBlockTable[batch][dstBlock]
        映射到 HBM 线性 token 下标
    每个复制项同时搬运 CKV（每 token 1024B）和 KPE（每 token 128B）两个片段。
    """
    # HBM 目标 KV cache：shape[0] 即 HBM 物理 block 数（hbmBlockCount），
    # 每个 block 128 个 token，按 token 线性排布。
    hbm_blocks = 1
    hbm_kv_cache = torch.full(
        (hbm_blocks, TOKENS_PER_BLOCK * CKV_WIDTH), SENTINEL, dtype=torch.bfloat16, device=device
    )
    hbm_k_rope = torch.full(
        (hbm_blocks, TOKENS_PER_BLOCK * KPE_WIDTH), SENTINEL, dtype=torch.bfloat16, device=device
    )
    # HBM block table：batch 0 的 dst block 0 -> physical block 0
    hbm_block_table = torch.tensor([[0]], dtype=torch.int32, device=device)
    # DRAM block table：offload slot 0 的 block 0 -> 对象起始 GVA
    dram_block_table = torch.tensor([[host_gva]], dtype=torch.int64, device=device)
    # offload slot：batch 0 对应对象 slot 0
    offload_slots = torch.tensor([0], dtype=torch.int32, device=device)
    # 复制计划：每项 (layerId, [源 token], [目标 token])。
    # layer0 复制 token 7/42，layer1 复制 token 11（对应原 sparse 版的两处偏移）。
    scatter_plan = [(0, [7, 42], [3, 6]), (1, [11], [5])]
    for layer_id, src_tokens, dst_tokens in scatter_plan:
        src_token_ids = torch.tensor(src_tokens, dtype=torch.int32, device=device)
        dst_slots = torch.tensor(dst_tokens, dtype=torch.int32, device=device)
        copy_counts = torch.tensor([len(src_tokens)], dtype=torch.int32, device=device)
        mf_acc_offload.npu_kvcache_scatter_copy(
            hbm_k_rope, hbm_kv_cache, None, None,
            hbm_block_table, dram_block_table,
            offload_slots, src_token_ids, dst_slots, copy_counts,
            ready_flag=None, layer_id=layer_id,
        )
    torch.npu.synchronize()
    # 校验复制回来的数据与原始对象对应 token 切片一致
    hbm_ckv = hbm_kv_cache.view(hbm_blocks * TOKENS_PER_BLOCK, CKV_WIDTH)
    hbm_kpe = hbm_k_rope.view(hbm_blocks * TOKENS_PER_BLOCK, KPE_WIDTH)
    checks = [(0, 7, 3), (0, 42, 6), (1, 11, 5)]
    for layer_id, src_token, dst_token in checks:
        if not torch.equal(hbm_ckv[dst_token], source_ckv[layer_id][src_token]):
            raise AssertionError(f"scatter_copy layer{layer_id} token{src_token} CKV mismatch")
        if not torch.equal(hbm_kpe[dst_token], source_kpe[layer_id][src_token]):
            raise AssertionError(f"scatter_copy layer{layer_id} token{src_token} KPE mismatch")


def _run_device(store, device_id, local_rank, host_rank, copy_mode):
    """Device 进程主流程：造数据 -> put 到 Host DRAM -> 查 GVA -> 回读 -> 校验 -> 清理。"""
    import torch
    from memfabric_hybrid import offload as mf_acc_offload

    device = torch.device(f"npu:{device_id}")
    # 1. 在本地 HBM 构造 KV cache 源数据
    source_ckv, source_kpe, source_layers = _make_source_layers(torch, device)
    torch.npu.synchronize()
    key = f"e2e-host-device-urma-{os.getpid()}"
    key_created = False
    lease_added = False
    copy_passed = False
    try:
        # 2. 把 HBM 数据写入 Host rank 的 DRAM 池
        _put_to_host(store, key, source_layers, host_rank)
        key_created = True
        # 3. 查对象元数据，拿到 Host DRAM 上的 GVA
        host_gva = _query_host_gva(store, key, host_rank)
        # 4. 加 lease（防止读取期间对象被回收）
        if store.batch_add_lease([key]) != [0]:
            raise AssertionError("batch_add_lease failed")
        lease_added = True
        # 5. 从 Host GVA 回读 HBM 并校验（按 --copy-mode 选择 sparse 或 scatter）
        if copy_mode == "sparse":
            _sparse_copy_and_verify(torch, mf_acc_offload, device, host_gva, source_ckv, source_kpe)
        else:
            _scatter_copy_and_verify(torch, mf_acc_offload, device, host_gva, source_ckv, source_kpe)
        copy_passed = True
    finally:
        # 无论成功失败都清理：释放 lease、删除 key
        cleanup_errors = []
        if lease_added:
            lease_ret = store.batch_remove_lease([key])
            if lease_ret != 0:
                cleanup_errors.append(f"batch_remove_lease ret={lease_ret}")
        if key_created:
            remove_results = store.remove_batch([key])
            if remove_results != [0]:
                cleanup_errors.append(f"remove_batch results={remove_results}")
        if copy_passed and cleanup_errors:
            raise AssertionError("; ".join(cleanup_errors))
    print(f"rank={local_rank} DEVICE PASS: host_rank={host_rank}, host_gva=0x{host_gva:x}", flush=True)


def _parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--role", required=True, choices=(ROLE_HOST, ROLE_DEVICE))
    parser.add_argument("--device-id", type=int, default=0, help="NPU runtime logical device id")
    parser.add_argument("--physical-device-id", type=int, help="Expected physical device id for EID validation")
    parser.add_argument("--host-eid", help="Host URMA EID; defaults to MF_HOST_URMA_EID")
    parser.add_argument("--device-eid", help="Device URMA EID; defaults to USE_LOCAL_EID")
    parser.add_argument("--copy-mode", choices=("sparse", "scatter"), default="scatter",
                        help="复制回读方式：sparse=sparse_copy_urma，scatter=npu_kvcache_scatter_copy")
    parser.add_argument("--provider-seconds", type=int, default=0, help="Host lifetime; 0 waits for Ctrl-C")
    return parser.parse_args()


def main():
    args = _parse_args()
    if args.device_id < 0 or args.provider_seconds < 0:
        raise ValueError("device-id and provider-seconds must be non-negative")
    if args.physical_device_id is not None and args.physical_device_id < 0:
        raise ValueError("physical-device-id must be non-negative")
    # 设置环境变量；Device 还需解析物理卡并切换 torch/ACL runtime device
    _configure_environment(args)
    if args.role == ROLE_DEVICE:
        _resolve_physical_device(args)
        _set_runtime_device(args.device_id)

    from memcache_hybrid import DistributedObjectStore

    store = DistributedObjectStore()
    initialized = False
    try:
        if store.setup(_make_local_config(args.role)) != 0:
            raise RuntimeError("store.setup failed")
        # init 会通过 world join barrier 等待两个进程都完成初始化，
        # 因此 init 返回即表示 Host 和 Device 都加入了同一个 2 节点 world。
        init_ret = store.init(args.device_id)
        if init_ret != 0:
            raise RuntimeError(f"store.init failed: ret={init_ret}")
        initialized = True
        actual_rank = store.get_local_service_id()
        if args.role == ROLE_DEVICE:
            # world_size=2 时 Host rank = 1 - device rank（无论谁拿到 0/1）
            host_rank = WORLD_SIZE - 1 - actual_rank
            _run_device(store, args.device_id, actual_rank, host_rank, args.copy_mode)
            return
        # Host 角色：打印就绪后保持进程存活，维持 DRAM 池在线
        print(f"rank={actual_rank} HOST_READY", flush=True)
        if args.provider_seconds > 0:
            time.sleep(args.provider_seconds)
        else:
            while True:
                time.sleep(3600)
    except KeyboardInterrupt:
        print(f"role={args.role} stopped", flush=True)
    finally:
        if initialized:
            store.close()


if __name__ == "__main__":
    main()
