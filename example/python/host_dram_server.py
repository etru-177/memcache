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

"""Host DRAM 常驻服务进程。

参照 example/python/test_mmc_host_device_urma.py 的 Host 角色实现：
启动后完成初始化（注册 DRAM 池到 MetaService），随后常驻后台，等待 Device 进程
把 HBM 数据卸载到本进程的 Host DRAM 池（world join 由 store.init 的 barrier 保证）。

用法示例：

  python3 host_dram_server.py \
      --max-dram-size 1GB --dram-size 1GB \
      --meta-url tcp://127.0.0.1:5000 \
      --config-store-url tcp://127.0.0.1:6000 \
      --world-size 2
"""

import argparse
import os
import time

# 介质类型：MEDIA_HBM=0, MEDIA_DRAM=1（与 C++ MediaType 枚举一致）
MEDIA_DRAM = 1
ROLE_HOST = "host"
# Host EID 自动派生用的固定前缀（62 hex）+ 末字节 host-index（保证多个 Host 进程唯一）
DEFAULT_HOST_EID_PREFIX = "aa" * 31


def _validate_eid(value, name):
    """校验 URMA EID：必须是 32 位十六进制且非全零，返回小写形式。"""
    if value is None or len(value) != 32 or any(character not in "0123456789abcdefABCDEF" for character in value):
        raise ValueError(f"{name} must be exactly 32 hexadecimal characters")
    if int(value, 16) == 0:
        raise ValueError(f"{name} must not be all zero")
    return value.lower()


def _configure_environment(host_eid):
    """设置 Host 角色的 MemFabric/HCOMM 环境变量。

    开启 HCOMM host-only、本地 DRAM validation、禁用未使用的 RDMA swap，
    并写入 Host URMA EID（未显式给定时按 host-index 自动派生唯一 EID）。
    """
    os.environ["HCOMM_HOST_ONLY"] = "1"
    os.environ["MF_LOCAL_DRAM_VALIDATION_ROLE"] = "host"
    os.environ["MF_HYBM_RDMA_SWAP_SPACE_SIZE"] = "0"
    os.environ["MF_HOST_URMA_EID"] = host_eid


def _make_local_config(args):
    """构造 Host 角色的 MemCache LocalService 本地配置。

    Host 只建 DRAM 池（dram_size），HBM 不申请；max_dram_size/max_hbm_size 定义
    共享 GVA 布局，保持各进程地址空间一致。
    """
    from memcache_hybrid import LocalConfig

    config = LocalConfig()
    config.meta_service_url = args.meta_url
    config.config_store_url = args.config_store_url
    config.hcom_url = os.getenv("MMC_HCOM_URL", "tcp://127.0.0.1:7000")
    config.protocol = "host_device_urma"
    config.world_size = args.world_size
    config.backend_id = f"host-dram-server-{os.getpid()}"
    config.max_dram_size = args.max_dram_size
    config.max_hbm_size = "1GB"
    config.dram_size = args.dram_size
    config.hbm_size = "0"
    return config


def _parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--max-dram-size", default="1GB", help="GVA 布局中 DRAM 段上限，默认 1GB")
    parser.add_argument("--dram-size", default="1GB", help="本进程实际注册的本地 DRAM 池大小，默认 1GB")
    parser.add_argument("--meta-url", default="tcp://127.0.0.1:5000", help="MetaService URL，默认 tcp://127.0.0.1:5000")
    parser.add_argument("--config-store-url", default="tcp://127.0.0.1:6000",
                        help="ConfigStore URL，默认 tcp://127.0.0.1:6000")
    parser.add_argument("--world-size", type=int, default=2, help="world 总进程数（Host+Device），默认 2")
    parser.add_argument("--host-index", type=int, default=0, help="Host 进程索引，用于派生唯一 Host EID，默认 0")
    parser.add_argument("--host-eid", help="Host URMA EID（32 hex）；默认按 host-index 自动派生")
    parser.add_argument("--provider-seconds", type=int, default=0, help="常驻时长；0 表示永久挂后台")
    return parser.parse_args()


def main():
    args = _parse_args()
    if args.world_size < 2:
        raise ValueError("world-size must be at least 2 (one Host + one Device)")
    if args.host_index < 0 or args.host_index >= args.world_size // 2:
        raise ValueError(f"host-index must be in [0, {args.world_size // 2})")
    if args.provider_seconds < 0:
        raise ValueError("provider-seconds must be non-negative")
    # 设置 Host 环境变量并派生唯一 Host EID
    if args.host_eid:
        host_eid = _validate_eid(args.host_eid, "Host EID")
    elif os.getenv("MF_HOST_URMA_EID"):
        host_eid = _validate_eid(os.getenv("MF_HOST_URMA_EID"), "Host EID")
    else:
        host_eid = DEFAULT_HOST_EID_PREFIX + f"{args.host_index + 1:02x}"
    _configure_environment(host_eid)

    from memcache_hybrid import DistributedObjectStore

    store = DistributedObjectStore()
    initialized = False
    try:
        if store.setup(_make_local_config(args)) != 0:
            raise RuntimeError("store.setup failed")
        # init 通过 world join barrier 等待所有 Host/Device 完成初始化后返回
        init_ret = store.init(0)
        if init_ret != 0:
            raise RuntimeError(f"store.init failed: ret={init_ret}")
        initialized = True
        actual_rank = store.get_local_service_id()
        # 打印就绪后常驻，维持 DRAM 池在线
        print(f"rank={actual_rank} HOST_READY: dram_size={args.dram_size} "
              f"max_dram_size={args.max_dram_size} eid={host_eid}", flush=True)
        if args.provider_seconds > 0:
            time.sleep(args.provider_seconds)
        else:
            while True:
                time.sleep(3600)
    except KeyboardInterrupt:
        print("host_dram_server stopped", flush=True)
    finally:
        if initialized:
            store.close()


if __name__ == "__main__":
    main()