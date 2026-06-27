---
name: memcache-code-review
description: "当用户要求 MemCache 代码审查、变更检查或新增代码检查时使用。按本项目已有的 doc/c_cpp_naming.md、.clang-format、.pre-commit-config.yaml、pre-commit/pyproject.toml、script/run_ut.sh、doc/SECURITYNOTE.md 和 API/安装文档检查变更。"
---

# memcache-code-review — 代码审查

## 目标

对新增或变更代码进行审查，确认变更符合本仓库已有规范、脚本和文档约束，并指出可能影响构建、测试、接口文档或安全说明的问题。

## 依据

审查只使用本项目已有依据：

- C/C++ 命名、魔法数字、测试命名：`doc/c_cpp_naming.md`。
- C/C++ 格式化：`.clang-format` 和 `.pre-commit-config.yaml`。
- Python 检查：`pre-commit/pyproject.toml`。
- UT 和覆盖率门禁：`script/run_ut.sh`。
- 安全说明：`doc/SECURITYNOTE.md`。
- 安装、配置、API 文档：`doc/install_whl.md`、`doc/install_run.md`、`doc/memcache_config.md`、`doc/memcache_c_api.md`、`doc/memcache_c++_api.md`、`doc/memcache_python_api.md`、`doc/memcache_restful_api.md`。

## 范围限定

- 重点审查新增或变更的代码行，不要求全文件重构。
- 旧代码存在同类问题时可以简要提示，但不要扩大修改范围。
- 建议应优先沿用项目现有宏、日志、错误码、命名和脚本。

## 审查要点

### 1. 命名和魔法数字

- 文件名、类型、函数、变量、常量、宏、枚举、测试文件、Mock 文件按 `doc/c_cpp_naming.md` 检查。
- 除 `0` 和 `-1` 等惯用值外，具有独立语义的数字必须提取为命名常量或枚举值。
- 测试代码也要检查魔法数字，尤其是端口、重试次数、等待次数、超时时间、倍率和阈值。

### 2. 格式化和静态检查

- C/C++ 行宽、缩进、括号和格式化行为以 `.clang-format` 与 `.pre-commit-config.yaml` 为准。
- Python 行宽、Ruff、Pylint、Bandit 配置以 `pre-commit/pyproject.toml` 为准。
- 如需给出验证建议，优先使用 `TARGET_BRANCH=develop bash script/ci-pre-commit-pr.sh` 或 `pre-commit run --files ...`。

### 3. 错误处理和日志风格

- 错误码和成功码沿用本项目已有语义，例如 `MMC_OK`、`MMC_ERROR`、`MMC_NOT_INITIALIZED`、`MMC_INVALID_ARGUMENT`、`MMC_UNMATCHED_KEY`。
- 错误分支应优先沿用本项目已有宏和日志风格，例如 `MMC_VALIDATE_RETURN`、`MMC_ASSERT*`、`MMC_LOG_ERROR`。
- 发现新增错误路径完全缺少可定位失败原因、返回码或关键上下文时，按风险指出，但不要引入项目中不存在的新日志框架或字段。

### 4. 测试和覆盖率

- C++ UT 以 `script/run_ut.sh` 为主要入口。
- 覆盖率门禁以脚本为准：行覆盖率不少于 70%，分支覆盖率不少于 40%。
- 新增 C++ 测试文件命名使用 `*_test.cpp`；存量 `test_*.cpp` 文件不要求仅因命名规则而修改。
- Python 测试如依赖 Ascend/NPU、MetaService、mock server 或 K8S 环境，未运行时必须明确说明。

### 5. 文档同步

- C API 变更检查 `doc/memcache_c_api.md`。
- C++ API 变更检查 `doc/memcache_c++_api.md`。
- Python API 变更检查 `doc/memcache_python_api.md`。
- REST/MetaService 行为变更检查 `doc/memcache_restful_api.md`。
- 安装、配置、环境变量或安全行为变更检查 `doc/install_whl.md`、`doc/install_run.md`、`doc/memcache_config.md`、`doc/SECURITYNOTE.md`。

### 6. 安全说明

- TLS、证书、私钥、端口范围、运行用户和文件权限相关变更，以 `doc/SECURITYNOTE.md` 为检查依据。
- 不建议输出未脱敏的私钥、证书口令、token、完整配置值、完整环境变量 dump 或原始缓存/共享内存数据。

## 输出格式

发现问题时，按严重程度排序：

```markdown
### 问题位置
`文件路径:行号`

### 问题
说明违反了本项目哪个已有规范、脚本或文档约束。

### 建议
给出最小修改建议，并说明依据文件。
```

没有发现问题时，说明已检查范围、依据文件和未运行的验证项。

## 原则

- 不新增本仓库没有的门禁规则。
- 不为了满足规则做无关重构或大范围格式化。
- 不把未运行的 UT、硬件验证、K8S 验证或性能验证描述为通过。
