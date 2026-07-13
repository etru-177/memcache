# MemCache Hybrid — 远端开发 & 全量运行指南

## 前置条件

- **远端服务器**：昇腾 NPU 服务器，已安装 NPU 驱动
- **本地机器**：VS Code + Remote—SSH 扩展
- **网络**：本地能 SSH 到远端服务器，远端能拉 gitcode + Docker 镜像

---

## 1. 远端克隆项目

SSH 登录远端服务器，克隆仓库：

```bash
git clone https://gitcode.com/Ascend/memcache.git
cd memcache
```

---

## 2. VS Code SSH-Remote 连接

不需要手动在远端启动 VS Code Server，VS Code 会自动处理。

**方式 A — 命令行快捷连接**（本地终端执行）：

```bash
code --remote ssh-remote+<你的服务器地址> /path/to/memcache
```

例如：

```bash
code --remote ssh-remote+192.168.1.100 /home/user/memcache
```

**方式 B — VS Code UI**：

1. `F1` → `Remote-SSH: Connect to Host...`
2. 输入 `ssh user@host` 或从 `~/.ssh/config` 选择
3. 输入服务器密码
4. 连接后 `File > Open Folder...` → 选择 `memcache` 目录
5. 输入服务器密码

> VS Code 会自动在远端安装 `vscode-server`，首次连接需等待几十秒。

---

## 3. 在 Dev Container 中打开

1. 确保远端已安装 Docker（root 或 docker 组权限）
2. VS Code 中 `F1` → `Dev Containers: Reopen in Container`
3. 输入服务器密码
4. 等待镜像拉取 + 容器构建 + postCreateCommand 完成

第一次会拉取 `quay.nju.edu.cn/ascend/vllm-ascend:v0.20.2rc1-a3`（约 15-20 GB），耗时较长。

> 官方镜像源为 quay.io/ascend/vllm-ascend:v0.22.1rc1-a3， 配置中使用中国国内镜像加速下载
> A2 环境请改用 `quay.nju.edu.cn/ascend/vllm-ascend:v0.20.2rc1`

postCreateCommand 会自动做（`.devcontainer/post_create.sh`）：

- 安装 Python 开发依赖（pre-commit、pytest、pybind11 等）
- 初始化 `3rdparty/` 子模块
- 验证工具链版本（gcc、cmake、ninja、python）
- CMake 冒烟配置测试
- 安装 pre-commit hooks

完成后 VS Code 左下角显示 `Dev Container: MemCache Dev Container`。

> 点击右下角的 `show logs` 会在终端显示构建细节和进度

---

## 4. 构建

```bash
# 首次构建（Release 模式）
bash script/build_and_pack_run.sh --build_mode RELEASE --build_test OFF

# 调试构建（复用已有 build/ 目录）
bash script/build_and_pack_run.sh --build_mode DEBUG --incremental
```

编译产物：
- **run 包**：`output/memcache_hybrid-*.run`
- **whl 包**：`output/memcache/wheel/memcache_hybrid-*.whl`

---

## 5. 运行全部 Examples

### 参数说明

| 参数 | 默认 | 说明 |
|---|---|---|
| *(无)* | — | Python + C++ 全量运行 |
| `--python` | — | 仅运行 Python 例子 |
| `--cpp` | — | 仅运行 C++ 例子（自动构建+安装运行包） |
| `--meta-config` | `config/mmc-meta.conf` | MetaService 配置文件路径 |
| `--local-config` | `config/mmc-local.conf` | LocalService 配置文件路径 |
| `--run-bench` | off | 运行性能基准测试 |
| `--example-timeout` | 180s | 单用例超时秒数 |
| `--dry-run` | off | 只打印不执行 |
| `--verbose` | off | 显示用例完整输出 |

### 使用示例

```bash
# 查看会跑哪些例子（不实际执行）
bash script/run_all_examples.sh --dry-run

# 全量运行（Python + C++，MetaService 自动检测/启动）
bash script/run_all_examples.sh

# 只跑 Python examples
bash script/run_all_examples.sh --python

# 只跑 C++ examples
bash script/run_all_examples.sh --cpp

# 运行时查看详细输出
bash script/run_all_examples.sh --verbose
```

### 运行内容

| 类别 | 条件 | 内容 |
|---|---|---|
| `example/python/` — 基础 CRUD | `--python` + NPU | `test_mmc_demo`, `test_mmc_batch`, `test_mmc_start_meta_service_and_simple_test` |
| `example/python/` — Layers | 同上 + torch_npu | `test_mmc_layers`, `test_mmc_layers_batch` |
| `example/cpp/` | `--cpp` + NPU | 构建并运行 `memcache_cpp_test`（put/get/remove 自动验证） |
| `example/benchmark/` | `--run-bench` + NPU | 4KB 小数据写/读冒烟测试 |

---

## 6. 手动运行示例

### Python 示例

```bash
# 方式一：先启动 MetaService，再运行
export MMC_META_CONFIG_PATH=/path/to/mmc-meta.conf
export MMC_LOCAL_CONFIG_PATH=/path/to/mmc-local.conf
python3 -c "from memcache_hybrid import MetaService; MetaService.main()" &

python3 example/python/test_mmc_demo.py
python3 example/python/test_mmc_batch.py

# 方式二：使用自包含示例（进程内启动 MetaService）
python3 example/python/test_mmc_start_meta_service_and_simple_test.py
```

### C++ 示例

```bash
# 需要先安装 run 包，source 环境变量
source /usr/local/memcache_hybrid/set_env.sh
source /usr/local/memfabric_hybrid/set_env.sh

cd example/cpp
mkdir -p build && cmake -B build && make -C build
export MMC_META_CONFIG_PATH=/path/to/mmc-meta.conf
export MMC_LOCAL_CONFIG_PATH=/path/to/mmc-local.conf
./build/memcache_cpp_test
```

### Benchmark

```bash
# 先启动 MetaService
export MMC_META_CONFIG_PATH=/path/to/mmc-meta.conf
bash example/benchmark/bench_start_meta.sh

# 写数据测试
export MMC_LOCAL_CONFIG_PATH=/path/to/mmc-local.conf
bash example/benchmark/bench_start.sh -t write -p 1 -b 16 -s 1048576 -n 100 -d 1 -e memcache -l npu

# 读数据测试
bash example/benchmark/bench_start.sh -t read -p 1 -b 16 -s 1048576 -n 100 -d 1 -e memcache -l npu
```

---

## 常见问题

**Q: 拉镜像太慢 / 超时**
`.devcontainer/Dockerfile` 中已将源指向国内 mirror `quay.nju.edu.cn`。如仍需代理，在远端 `/etc/docker/daemon.json` 配置 registry mirror。

**Q: 构建时找不到 `pybind11`**
`postCreateCommand` 已安装，如跳过则手动：

```bash
pip install pybind11
```

**Q: 没有 NPU 能不能跑**
不能。MemCache 依赖昇腾 NPU 驱动（`davinci`、`devmm_svm`）。无 NPU 环境下构建会失败。

**Q: 只想用 Python 不用 C++ 例子**
```bash
bash script/run_all_examples.sh --python
```

**Q: 需要修改 MetaService 或 LocalService 配置**
编辑 `config/mmc-meta.conf` 和 `config/mmc-local.conf`，或通过 `--meta-config` / `--local-config` 指定自定义路径。
