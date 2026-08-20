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

本进程不依赖 torch。MemCache 配置由 MMC_LOCAL_CONFIG_PATH 指定的配置文件加载；
Host URMA EID 通过 --eid 参数提供。

用法示例：

  export MMC_LOCAL_CONFIG_PATH=/path/to/mmc-local.conf
  python3 host_dram_server.py --eid 0123456789abcdef0123456789abcdef

配置文件至少应包含 meta_service_url、config_store_url、world_size、protocol、
dram.size、max.dram.size、hbm.size 和 max.hbm.size 等 LocalService 配置。
"""

import argparse
import os
import time

ANSI_BOLD_GREEN = "\033[1;92m"
ANSI_RESET = "\033[0m"


def _require_environment(name):
    value = os.getenv(name)
    if not value:
        raise RuntimeError(f"environment variable {name} is required")
    return value


def _validate_eid(value):
    """校验 Host URMA EID：必须是 32 位十六进制且非全零。"""
    if len(value) != 32 or any(character not in "0123456789abcdefABCDEF" for character in value):
        raise ValueError("--eid must be exactly 32 hexadecimal characters")
    if int(value, 16) == 0:
        raise ValueError("--eid must not be all zero")
    return value.lower()


def _configure_host_environment(eid):
    """设置固定的 Host 角色环境，并返回配置路径和 Host EID。"""
    config_path = _require_environment("MMC_LOCAL_CONFIG_PATH")
    if not os.path.isfile(config_path):
        raise FileNotFoundError(f"MMC_LOCAL_CONFIG_PATH does not exist: {config_path}")
    host_eid = _validate_eid(eid)
    os.environ["HCOMM_HOST_ONLY"] = "1"
    os.environ["MF_LOCAL_DRAM_VALIDATION_ROLE"] = "host"
    os.environ.setdefault("MF_HYBM_RDMA_SWAP_SPACE_SIZE", "0")
    os.environ["MF_HOST_URMA_EID"] = host_eid
    return config_path, host_eid


def _parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eid", required=True, help="Host URMA EID，必须为 32 位非零十六进制字符")
    return parser.parse_args()


def main():
    args = _parse_args()
    config_path, host_eid = _configure_host_environment(args.eid)
    # 直接导入 C++ Python 扩展，避免 memcache_hybrid -> memfabric_hybrid -> torch 的导入链。
    from _pymmc import DistributedObjectStore

    store = DistributedObjectStore()
    initialized = False
    try:
        # 不调用 setup：mmc_init 会读取 MMC_LOCAL_CONFIG_PATH 指向的完整配置文件。
        init_ret = store.init(0)
        if init_ret != 0:
            raise RuntimeError(f"store.init failed: ret={init_ret}")
        initialized = True
        actual_rank = store.get_local_service_id()
        print(
            f"{ANSI_BOLD_GREEN}rank={actual_rank} HOST_READY: "
            f"config={config_path} eid={host_eid}{ANSI_RESET}",
            flush=True,
        )
        print("READY", flush=True)
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        print("host_dram_server stopped", flush=True)
    finally:
        if initialized:
            store.close()


if __name__ == "__main__":
    main()
