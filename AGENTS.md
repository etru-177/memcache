# 仓库指南

## 构建

- **默认打包构建**：`bash script/build_and_pack_run.sh --build_mode RELEASE --build_test OFF`，生成 `.run` 包和 wheel 到 `output/`。
- **调试构建**：`bash script/build_and_pack_run.sh --build_mode DEBUG --incremental`，用于本地调试并复用已有 `build/`。
- 编译统一使用 `build_and_pack_run.sh`，不要直接调用底层构建脚本。

## 测试

- **全部 C++ UT**：`bash script/run_ut.sh`，执行 DEBUG 测试构建、运行 `output/bin/ut/test_mmc_test`，并生成 `output/coverage`。
- **按名称过滤**：`bash script/run_ut.sh meta_service`，脚本会传递 `--gtest_filter=*meta_service*`。
- 覆盖率门禁来自 `script/run_ut.sh`：`src/memcache` 行覆盖率不少于 70%，分支覆盖率不少于 40%。
- Python 测试位于 `test/python`，通常依赖 Ascend/NPU、MetaService 或 mock server；未准备环境时不要默认视为已通过。

## 预提交与代码风格

- 安装：`pip install pre-commit && pre-commit install --install-hooks`。
- PR 增量检查：`bash script/ci-pre-commit-pr.sh`。
- Python 规则以 `pre-commit/pyproject.toml` 为准：Ruff 目标 `py310`，行宽 120，并启用 Pylint、Bandit。
- C/C++ 格式以 `.clang-format` 和 `.pre-commit-config.yaml` 为准：clang-format v18.1.8、4 空格缩进、行宽 120。
- C/C++ 命名和魔法数字规则以 `doc/c_cpp_naming.md` 为准。

## 代码规范

- 新增 C/C++ 代码遵守 `doc/c_cpp_naming.md`：文件 `snake_case`，C++ 类型/函数 `UpperCamelCase`，C API `snake_case`，参数和局部变量 `lowerCamelCase`，私有/保护成员以 `_` 结尾。
- 除 `0`、`-1` 等惯用值外，具有独立语义的数字必须提取为命名常量或枚举值；测试代码同样适用。
- 新增测试文件使用 `*_test.cpp`，Mock 实现文件使用 `mock_*.cpp`；存量文件不要求为此单独改名。
- 错误处理保持本项目现有错误码、断言宏、校验宏和日志宏风格，例如 `MMC_OK`、`MMC_ERROR`、`MMC_VALIDATE_RETURN`、`MMC_LOG_ERROR`。
- 安全相关要求以 `doc/SECURITYNOTE.md` 为准，涉及 TLS、证书、私钥、配置文件权限、运行用户和端口范围时必须同步参考。

## 运行时与配置

- 常用配置文件：`config/mmc-meta.conf`、`config/mmc-local.conf`。
- 安装使用文档：`doc/install_whl.md`、`doc/install_run.md`。
- 配置项说明：`doc/memcache_config.md`。
- 常见环境变量：`MMC_META_CONFIG_PATH`、`MMC_LOCAL_CONFIG_PATH`、`MEMCACHE_HYBRID_HOME_PATH`、`LD_LIBRARY_PATH`。
- 构建时 `PYTHON_HOME` 默认探测 `/usr/local/python3.11` 或 `/usr/local`；UT 会设置 mock CANN 的 `ASCEND_HOME_PATH`。

## 仓库模块边界

| 模块 | 路径 | 说明 |
|---|---|---|
| 公共 C/C++ API | `src/memcache/include/` | 对外头文件，变更需同步 API 文档 |
| 核心实现 | `src/memcache/csrc/` | client、common、config、entities、local/meta service、net、proto、under_api |
| Python 绑定 | `src/memcache/python/`、`src/memcache/csrc/python_wrapper/` | `memcache_hybrid` wheel 和 `_pymmc` |
| MetaService 进程 | `src/memcache/csrc/daemon/` | `mmc_meta_service` |
| 依赖底座 | `3rdparty/memfabric_hybrid/` | MemFabric/SMEM/HYBM 依赖 |
| 测试与示例 | `test/`、`example/` | GTest、Python/mock、K8S deploy、C++/Python 示例 |

## 对外接口与文档同步

- C API 变更同步 `doc/memcache_c_api.md`。
- C++ API 变更同步 `doc/memcache_c++_api.md`。
- Python API 变更同步 `doc/memcache_python_api.md`。
- REST/MetaService 行为变更同步 `doc/memcache_restful_api.md`。
- 安装、配置、环境变量或安全行为变更同步 `doc/install_whl.md`、`doc/install_run.md`、`doc/memcache_config.md`、`doc/SECURITYNOTE.md` 中相关内容。

## 产物与依赖

- 生成产物不要提交：`build/`、`output/`、`src/memcache/python/build/`、`*.egg-info`、wheel、shared libs。
- 关键依赖来自现有构建脚本和文档：CMake >= 3.12、C++17 编译器、Ninja 或 Make、Python >= 3.8、pybind11、MemFabric Hybrid。

## 许可证

Mulan PSL v2。新增源码文件保持与现有文件一致的版权和许可证头。
