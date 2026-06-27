---
name: memcache-release
description: "生成 MemCache 变更日志或发布说明时使用。只根据版本、日期、提交/PR 差异和用户提供的变更信息整理发布说明，不负责发布检查、构建矩阵、产物验证或实际发布执行。"
---

# MemCache 发布说明

## 适用范围

此技能只用于生成 MemCache / memcache_hybrid 的变更日志或发布说明。

不负责：

- 创建或维护发布检查清单。
- 修改版本号、发布分支、tag 或发布页面。
- 构建 `.run` 包、wheel 或发布产物。
- 上传 PyPI、创建 GitCode/GitHub release 或执行实际发布。
- 判断未运行的测试、硬件门禁、构建矩阵或性能验证是否通过。

## 需要收集的信息

生成前先确认：

- `VERSION`：目标版本，例如 `1.0.1`。
- `YYYY-MM-DD`：发布日期；用户未指定时使用当前日期。
- 对比范围：上一版本 tag/commit 到当前版本 tag/commit，或用户提供的 PR/commit 列表。
- 本次发布所有新增 PR 的编号、链接和标题。
- 用户整理的重点变更；如有，优先使用用户整理内容，并用 git/PR 差异补充遗漏。

缺少对比范围或 PR 列表时，先通过本地 git、远端 PR 信息或用户补充确认；无法确认时在结果中明确标注信息缺失。

## 生成原则

- 只包含有变化的章节；空章节必须省略。
- 面向用户描述行为变化、使用影响和兼容性影响，不机械罗列 commit。
- 同一条变更只放入最合适的一个章节，避免重复。
- 保留必要的 API 名称、配置项、环境变量、命令、路径、错误码和 PR 编号。
- 不确定分类时，优先放入 `Changed`，并在需要时说明依据。
- `References` 必须覆盖本次发布所有新增 PR。

## 分类规则

- `Summary`：本次发布的整体摘要。
- `Feature`：新增能力、新 API、新运行模式、新平台/硬件支持、新工具或新集成。
- `Changed`：已有行为、配置、构建、性能、日志、默认值、内部实现或非 bug 修复类调整。
- `Fixed`：bug 修复、稳定性修复、错误处理修复、崩溃、hang、timeout、数据错误修复。
- `Compatibility`：ABI/API、Python 版本、硬件/驱动/CANN、依赖、包名、安装方式或行为兼容性说明。
- `Documentation`：README、安装说明、API 文档、配置文档、示例、发布文档更新。
- `References`：本次发布所有新增 PR。

## 输出格式

严格使用以下格式。除 `Installation` 和 `References` 外，仅包含有变化的章节；没有变化的章节不要输出。

```markdown
# MemCache v{VERSION} - {YYYY-MM-DD}

## Installation

- PyPI: [memcache-hybrid](https://pypi.org/project/memcache-hybrid/)
- Install: `pip install memcache-hybrid`

## Summary

本次发布摘要

## Feature

- 新增能力描述

## Changed

- 变更描述

## Fixed

- 修复描述

## Compatibility

- 兼容性说明

## Documentation

- 文档变更描述

## References

- [!123](https://gitcode.com/Ascend/memcache/pull/123) — PR 标题
```

## 参考规则

- PR 按编号升序或合入时间排序；同一份发布说明中保持一种排序方式。
- PR 标题使用实际标题，不要改写到失真。
- 如果 PR 链接不可用，但编号和标题可确认，仍按标准格式输出链接。
- 如果某个变更来自直接 commit 而非 PR，应在 `References` 后补充说明，不要伪造 PR。

## 最终检查

输出前确认：

- 标题版本和日期正确。
- 空章节已删除。
- `Installation` 中的 PyPI 链接和安装命令保持固定格式。
- `References` 覆盖本次发布所有新增 PR。
- 每条变更描述都能对应到 PR、commit 或用户提供的发布信息。
- 未运行的验证项没有被描述为通过。
