# 两进程 MemCache Host-Device URMA 用例

`test_mmc_host_device_urma.py` 使用两个进程验证一条 Host DRAM 到 NPU HBM 的完整路径：

```text
Device HBM tensor
  -> batch_put_from_layers 写入 Host rank 0 DRAM
  -> batch_get_key_info 获取 Host GVA
  -> batch_add_lease
  -> sparse_copy_urma 读取到 Device HBM tensor
  -> 数据比较
```

## 前置条件

1. MetaService 和 config store 已启动，默认地址分别为 `127.0.0.1:5000` 和 `127.0.0.1:6000`。
2. MemFabric 使用 local DRAM validation 构建，MemCache 支持 `host_device_urma`。
3. HCOMM 使用 `--pkg --full --experimental` 构建并安装，Device 环境中存在：

```text
${ASCEND_HOME_PATH}/opp/built-in/op_impl/aicpu/config/ccl_kernel.json
```

4. 上一轮进程和旧 world 已清理。

## 公共环境

```bash
source /path/to/cann/set_env.sh
source /path/to/memfabric/set_env.sh
source /path/to/memcache/set_env.sh

export MMC_META_SERVICE_URL=tcp://127.0.0.1:5000
export MMC_CONFIG_STORE_URL=tcp://127.0.0.1:6000
export MMC_HCOM_URL=tcp://127.0.0.1:7000
export MEMFABRIC_HYBRID_EXTEND_LIB_PATH=/path/to/memfabric/lib64
```

不需要 `mmc-local.conf`。示例使用 Python `LocalConfig` 设置 `world_size=2`、1 GiB DRAM/HBM GVA 布局和
`host_device_urma`。

## 启动

示例不依赖固定 rank。每个进程初始化后调用 `get_local_service_id()` 获取自身 rank；world_size=2 时 Host rank
即 `1 - device_rank`，Device 自动推导 Host rank 用于写入目标和位置校验。因此 Host/Device 分别拿到 rank 0/1 或
1/0 都可以。

Host 是纯 CPU 进程，不依赖 torch/NPU；只有 Device 需要 NPU。两个 `store.init()` 会互相等待 world 成员，
因此 `HOST_READY` 要等 Device 加入后才出现，不能以它作为 Device 的启动信号。

在终端 1 先启动 Host：

```bash
python3 example/python/test_mmc_host_device_urma.py \
  --role host \
  --device-id 0 \
  --host-eid <HOST_EID>
```

脚本会在 Host 进程内设置 `HCOMM_HOST_ONLY=1`、`MF_LOCAL_DRAM_VALIDATION_ROLE=host` 和
`MF_HYBM_RDMA_SWAP_SPACE_SIZE=0`，并输出：

```text
rank=<host_rank> HOST_READY
```

等待 2-3 秒后再启动 Device：

```bash
ASCEND_RT_VISIBLE_DEVICES=0 \
python3 example/python/test_mmc_host_device_urma.py \
  --role device \
  --device-id 0 \
  --physical-device-id 0 \
  --device-eid <DEVICE_EID>
```

示例不额外做 probe 握手：Device 的正式流程（写入 Host DRAM、查回 GVA、sparse copy 并校验）本身就隐式验证了
Host DRAM 可写且 URMA 建链就绪。Device 完成并清理后输出：

```text
rank=<device_rank> DEVICE PASS: host_rank=<host_rank>, host_gva=0x...
```

Device 退出后用 `Ctrl-C` 停止 Host（或设置 `--provider-seconds N` 让 Host 自动退出）。

使用 `ASCEND_RT_VISIBLE_DEVICES=<physical-id>` 隔离设备时，进程内 `--device-id` 通常为 `0`，而
`--physical-device-id` 填真实物理卡号。脚本会校验两者映射；Host/Device EID 必须通过 EID 查询工具从该物理卡
获得。

`--host-eid` 和 `--device-eid` 也可以分别通过 `MF_HOST_URMA_EID`、`USE_LOCAL_EID` 提供。
