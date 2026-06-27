# MemCache Agent 技能

本目录保存面向 MemCache 仓库的通用 Agent 技能。每个技能都是独立目录，核心文件为 `SKILL.md`。

## 技能列表

| 技能 | 适用场景 | 示例用法 |
|---|---|---|
| `memcache-code-review` | 代码审查，按本仓库已有脚本、文档和配置检查变更 | `使用 memcache-code-review 检查这次变更` |
| `memcache-release` | 生成 MemCache 变更日志或发布说明，并列出本次发布新增 PR | `使用 memcache-release 生成 1.0.1 发布说明` |

## 使用方式

- 需要某个能力时，在请求中显式写出技能名，例如：`使用 memcache-release ...`。
- 不确定该用哪个技能时，描述任务目标即可；Agent 应根据 `SKILL.md` 的 `description` 自动选择最匹配的技能。
- 一个任务可以组合多个技能，例如发布前可同时使用 `memcache-code-review` 和 `memcache-release`。
- 修改或新增技能时，只编辑对应目录下的 `SKILL.md`；本仓库不保留 `openai.yaml` 等平台专用 UI 元数据。

## 免责声明

- 技能是 Agent 的工作指南，不是自动化测试或质量保证的替代品。
- 涉及 Ascend/NPU、RDMA/URMA、UBSIO、K8S HA、多节点、性能数据的结论，必须以实际环境验证结果为准。
- 发布、基准测试、集成适配等流程中，未运行的验证项必须在最终结论中明确说明，不能默认视为通过。
- 技能中的命令和路径基于当前仓库结构维护；仓库脚本、目录或构建参数变化时，应同步更新对应 `SKILL.md`。
- 对外接口、环境变量、安装方式、发布内容等变更仍需人工 review，不能仅凭技能说明直接发布。
