# C/C++ 编码规范

## 最高优先级：魔法数字（magic number）

- 除 `0` 和 `-1`（作为无效值/错误码的惯用法）外，代码中不允许出现其他直接数字字面量
- 所有具有独立语义的数值都必须提取为命名常量或枚举值
- 测试代码同样适用，尤其是端口、重试次数、等待次数、超时时间、倍率和阈值
- 修改或新增代码时，应优先检查本规则；即使其他命名规则全部满足，也不能忽视魔法数字

示例：

```cpp
if (retryCount > 3) { ... }        // wrong: magic number
int timeoutMs = 5000;              // wrong: magic number

constexpr int kMaxRetryCount = 3;
if (retryCount > kMaxRetryCount) { ... }  // correct

constexpr int kDefaultTimeoutMs = 5000;
int timeoutMs = kDefaultTimeoutMs;        // correct
```

## 适用范围与总则

- 同一语义在同一层级只使用一种命名风格
- 新增代码必须优先遵守本规范
- 命名应优先表达业务语义，避免无意义缩写
- 除兼容既有公共接口外，不引入新的混合命名风格

## 快速对照表

| 对象 | 命名规则 | 示例 |
| --- | --- | --- |
| 文件 | `snake_case` | `mmc_types.h` |
| include guard | `UPPER_SNAKE_CASE` | `MEM_FABRIC_MMC_TYPES_H` |
| namespace | 全小写 | `ock::mmc` |
| 类型 | `UpperCamelCase` | `MmcMetaManager` |
| C++ 函数 | `UpperCamelCase` | `LoadFromFile` |
| C 接口函数 | `snake_case` | `mmc_init` |
| 参数 / 局部变量 | `lowerCamelCase` | `defaultTtl` |
| public 成员变量 | `lowerCamelCase` | `defaultTtlMs` |
| private / protected 成员变量 | `lowerCamelCase_` | `defaultTtlMs_` |
| 全局变量 | `g` + `UpperCamelCase` | `gLogger` |
| 非局部常量 | `k` + `UpperCamelCase` | `kDefaultLimit` |
| 局部常量 | `lowerCamelCase` | `retryCount` |
| 宏 / 枚举值 | `UPPER_SNAKE_CASE` | `MMC_OK` |
| 魔法数字 | 独立语义数值必须提取为命名常量或枚举值 | `kHttpTestRetryCount` |
| 测试文件 | `*_test.cpp` | `mmc_configuration_test.cpp` |
| Mock 文件 | `mock_*.cpp` | `mock_smem.cpp` |

## 文件与编译单元

### 文件命名

- 文件名使用 `snake_case`
- 只使用 `.h`、`.c` 和 `.cpp` 三种文件后缀
- 当 C++ 文件主要承载单个类时，文件名应与该类语义对应，并将 `UpperCamelCase` 类型名映射为 `snake_case` 文件名

示例：

- `mmc_types.h`
- `mmc_configuration.cpp`
- `test_meta_manager.cpp`
- `mock_dfc_api.cpp`

### 头文件保护

- 头文件统一使用 include guard
- guard 宏名使用 `UPPER_SNAKE_CASE`
- guard 宏名应由模块路径和文件名组合而成
- 不使用保留标识符风格
- 存量代码中已有的双下划线风格（如 `__MEMFABRIC_MMC_DEF_H__`）在本次规范中不要求立即迁移，新增文件必须遵守新风格

示例：

```c
#ifndef MEM_FABRIC_MMC_TYPES_H
#define MEM_FABRIC_MMC_TYPES_H

/* declarations */

#endif
```

### namespace 命名

- namespace 使用全小写
- 多级 namespace 按模块层级组织
- namespace 名称使用简洁、稳定的业务词汇
- namespace alias 使用全小写或 `lower_snake_case`
- inline namespace 按 namespace 规则命名

示例：

- `ock`
- `ock::mmc`
- `ock::mf`
- `shm`
- `namespace mmc_detail = ock::mmc::detail`

## 程序实体命名

### 类型命名

- `class` 使用 `UpperCamelCase`
- `struct` 使用 `UpperCamelCase`
- `union` 使用 `UpperCamelCase`
- `enum` 和 `enum class` 类型名使用 `UpperCamelCase`
- `typedef` 和 `using` 类型别名使用 `UpperCamelCase`
- 嵌套类型、局部类型、匿名 `struct` / `union` 关联的具名字段，分别按类型和成员变量规则命名

示例：

- `Configuration`
- `MmcMetaManager`
- `MmcLocation`
- `EvictResult`
- `MmcUbsIoProxyPtr`
- `PayloadUnion`

### C++ 函数

- 成员函数使用 `UpperCamelCase`
- 普通自由函数使用 `UpperCamelCase`
- 静态函数使用 `UpperCamelCase`
- 构造函数和析构函数使用 C++ 语法要求的类名，不额外改变命名
- 重载运算符函数使用 C++ 语法要求的 `operator` 名称
- 用户自定义转换函数使用 C++ 语法要求的 `operator Type` 名称，其中 `Type` 按类型规则命名
- 用户自定义字面量运算符如需新增，后缀使用 `_lower_snake_case`
- `main`、标准库替换函数、第三方回调入口等由语言或外部接口指定的函数名，按其接口要求命名

示例：

- `Setup`
- `LoadFromFile`
- `Start`
- `Mount`
- `GenerateOperateId`
- `CreateLocalConfigWithCurrentDefaults`
- `operator==`
- `operator bool`
- `operator ""_mb`

### C 接口函数

- C 导出接口使用 `snake_case`
- C 风格工具函数使用 `snake_case`

示例：

- `mmc_setup`
- `mmc_init`
- `mmc_uninit`
- `smem_bm_config_init`
- `smem_init`

> **C 接口 struct typedef 说明**：C 导出头文件中的 `struct` typedef（如 `mmc_tls_config`、`mmc_client_config_t`）属于外部
> API，为保持接口兼容性，不要求迁移命名风格。新增 C 接口 struct typedef 应使用 `snake_case` 并与函数命名保持一致。

### 参数

- 参数使用 `lowerCamelCase`
- 函数参数、模板中的函数参数、lambda 参数、catch 参数均按参数规则命名
- 未使用参数可省略参数名；必须保留时仍使用 `lowerCamelCase`

示例：

- `defaultTtl`
- `config`
- `filePath`
- `urlString`

### 局部变量

- 局部变量使用 `lowerCamelCase`
- 字符串优先使用 `std::string`；仅在 C 接口、底层兼容或外部接口要求时使用 `char*` / `const char*`
- 引用变量、指针变量、数组变量、range-for 变量、结构化绑定变量均按局部变量规则命名
- lambda 对象变量按局部变量规则命名

示例：

- `tmpStr`
- `localTime`
- `tlsConfig`
- `buffer`
- `statusRef`
- `bufferPtr`
- `objectIds`
- `[rankId, deviceId]`

```cpp
auto processCallback = [config](int value) { /* ... */ };
auto onComplete = [&]() { /* ... */ };
```

### 成员变量

- `public` 成员变量使用 `lowerCamelCase`
- `private` / `protected` 成员变量使用 `lowerCamelCase_`
- 私有和受保护成员变量统一使用尾部下划线 `_`
- 非静态数据成员、非常量静态数据成员、bit-field 均按成员变量规则命名
- 类静态常量按非局部常量规则命名
- 兼容既有公共接口或外部 ABI 时可保留已有名称

示例：

- `rank`
- `mediaType`
- `defaultTtlMs`
- `rank_`
- `mediaType_`
- `started_`
- `defaultTtlMs_`
- `retryCount_`
- `kDefaultTtlMs`

### 全局变量

- 全局变量使用 `g` 前缀
- 前缀后名称主体使用 `UpperCamelCase`
- 仅在确有必要时引入全局状态
- namespace 作用域的可变变量按全局变量规则命名
- `thread_local` 可变变量按其作用域分别遵守全局变量、成员变量或局部变量规则

示例：

- `gOneNumber`
- `gRequestIdGenerator`
- `gDefaultTimeoutMs`
- `gLogger`

### 函数内静态变量

- 函数内静态变量使用 `lowerCamelCase`
- 不使用 `g_` 前缀
- 函数内静态常量按局部常量规则命名

示例：

- `localTime`
- `retryCount`
- `cacheReady`

## 常量、宏与枚举

### 非局部常量

- 非局部常量统一使用 `k` 前缀 + `UpperCamelCase`
- 包括全局常量、命名空间常量、类静态常量、跨函数复用常量
- 变量模板如表示常量，使用 `k` 前缀 + `UpperCamelCase`

示例：

- `kConfMust`
- `kDramSizeAlignment`
- `kHbmSizeAlignment`
- `kReturnOk`
- `kHttpTestPortBase`
- `kHttpTestRetryCount`
- `kHttpTestRetryIntervalMs`
- `kLogRotationFileSize`
- `kEvictThresholdHigh`
- `kEvictThresholdLow`

### 局部常量

- 局部常量使用 `lowerCamelCase`
- 仅在兼容已有接口或协议时保留特殊命名

示例：

- `defaultTtl`
- `retryCount`

### 宏

- 宏名使用 `UPPER_SNAKE_CASE`
- 宏函数名使用 `UPPER_SNAKE_CASE`
- 宏参数使用 `UPPER_SNAKE_CASE`
- 宏名必须表达清晰语义

示例：

- `MMC_DATA_TTL_MS`
- `MMC_LOG_ERROR`
- `MMC_ASSERT`
- `TP_TRACE_BEGIN`
- `LOG_ERROR`

### 枚举类型

- 枚举类型名使用 `UpperCamelCase`
- 匿名枚举只允许用于局部兼容场景；优先使用具名枚举或命名常量

示例：

- `MmcErrorCode`
- `EvictResult`
- `MemUnit`
- `ConfValueType`

### 枚举值

- 枚举值使用 `UPPER_SNAKE_CASE`
- 类内未限定作用域枚举的枚举值同样使用 `UPPER_SNAKE_CASE`

示例：

- `MMC_OK`
- `MMC_ERROR`
- `MEDIA_HBM`
- `REMOVE`
- `MOVE_DOWN`
- `ACL_MEM_LOCATION_TYPE_HOST`

## 模板与其他 C++17 语言元素

### 模板名称

- 类模板、别名模板使用 `UpperCamelCase`
- 函数模板使用 `UpperCamelCase`
- 变量模板按变量或常量语义分别遵守变量命名或常量命名规则
- 成员模板按其声明实体分别遵守成员函数、成员变量、类型或别名规则
- 显式特化和偏特化不引入新的命名风格，保持主模板名称一致

示例：

- `ObjectPool`
- `MmcUbsIoProxyPtr`
- `CreateLocalConfig`
- `kDefaultLimit`

### 模板参数

- 类型模板参数使用 `UpperCamelCase`
- 短小泛型类型参数可使用 `T`、`U`、`V`
- 非类型模板参数使用 `lowerCamelCase`
- 模板模板参数使用 `UpperCamelCase`
- 参数包按对应参数类别命名，并优先使用复数或 `...` 语义清晰的名称

示例：

- `ValueType`
- `Allocator`
- `bufferSize`
- `Container`
- `ValueTypes`
- `args`

### lambda 捕获

- lambda 捕获不引入新的命名风格，被捕获变量保持原变量名
- init-capture 引入的新变量使用 `lowerCamelCase`

示例：

- `[config]`
- `[localConfig = config]`

### using 声明与 using directive

- `using` 声明和 `using namespace` 不重命名实体，被引入实体保持原名称
- 不在头文件中新增 `using namespace`

### 自定义 attribute

- 不新增自定义 attribute 或自定义 attribute namespace
- 兼容外部接口必须使用时，按外部接口要求命名

## 资源管理相关规则

### 智能指针

- 优先使用 `std::make_unique` 创建 `std::unique_ptr`
- 优先使用 `std::make_shared` 创建 `std::shared_ptr`
- 不要直接使用 `new` 创建对象
- 如需处理内存分配失败，使用 `new (std::nothrow)` 后立即封装为智能指针

## 测试与 Mock 命名

### 测试文件

- 测试文件使用 `snake_case`
- 测试文件统一使用 `*_test.cpp`
- `*_test.cpp` 更符合主流 C/C++ 开源项目习惯
- 存量测试文件（如 `test_meta_manager.cpp`）不要求立即重命名，新增测试文件必须使用 `*_test.cpp` 后缀

示例：

- `mf_file_util_test.cpp`
- `smem_last_error_test.cpp`
- `mmc_configuration_test.cpp`
- `meta_manager_test.cpp`

### Mock 文件

- Mock 实现文件使用 `mock_*.cpp`

示例：

- `mock_dfc_api.cpp`
- `mock_smem.cpp`
- `mock_smem_bm.cpp`

### 测试夹具类型

- 测试夹具类使用 `UpperCamelCase` + `Test`

示例：

- `MetaConfigUtilsTest`
- `MmcConfigurationTest`
- `BmInitTest`
- `MmcMetaManagerTest`

### 测试用例名

- 测试用例名使用具备行为语义的 `UpperCamelCase`
- 名称应尽量直接表达前置条件、操作和预期结果，避免使用信息不足的泛化命名

示例：

- `CreateDefaultMetaConfigReturnsExpectedDefaults`
- `ValidateTLSConfigWithMissingCertReturnsError`

## 规范执行

### 静态检查

- 使用 `clang-format` 进行代码格式化，项目根目录已提供 `.clang-format` 配置
- 使用 `clang-tidy` 进行命名和代码质量检查，按需补充 check 配置
- 修改或新增代码后，执行 `git diff HEAD -U0 | clang-format-diff -p1 -i` 格式化变更行
