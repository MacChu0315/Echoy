# Mosaico 开发与体验技能

`skills/mosaico-development` 统一维护 8 项可独立使用的技能：5 项工程技能和 3 项体验与设计试用技能。本分支只包含技能、参考资料和检查工具；Echoy 产品代码在同仓库的 `master` 与 `archive/echoy-three-device` 分支，产品版本由对应 tag 保存。技能文档不代表产品功能已验收。

## 按任务选择

| 当前任务 | Skill | 路径 | 验证状态 |
|---|---|---|---|
| 探索产品方向、制作核心体验、向真机交接 | `prototyping-mosaico-experiences` | [体验原型](skills/prototyping-mosaico-experiences/SKILL.md) | 试用；行为对照未执行 |
| 在认可基线上修改布局、视觉和手势 | `iterating-mosaico-ui` | [界面迭代](skills/iterating-mosaico-ui/SKILL.md) | 试用；行为对照未执行 |
| 定义拼接、换位、取消和异步准备应如何表现 | `modeling-mosaico-multi-device-interactions` | [多机状态设计](skills/modeling-mosaico-multi-device-interactions/SKILL.md) | 试用；行为对照未执行 |
| 将认可原型落实到共享 C/GSP，检查模拟器与真机界面差异 | `mosaico-gsp-parity` | [界面还原](.agents/skills/mosaico-gsp-parity/SKILL.md) | 历史有限情景检查；本次包检查 |
| 排查触摸卡顿、音频超时、内部内存不足 | `mosaico-runtime-performance` | [运行性能](.agents/skills/mosaico-runtime-performance/SKILL.md) | 历史有限情景检查；本次包检查 |
| 实现或排查接触、无线、时钟和共享状态 | `mosaico-peer-cooperation` | [设备协作](.agents/skills/mosaico-peer-cooperation/SKILL.md) | 历史有限情景检查；本次包检查 |
| 验证合成与 PCM，分析扬声器响度、音色和失真 | `mosaico-audio-validation` | [音频验证](.agents/skills/mosaico-audio-validation/SKILL.md) | 历史有限情景检查；本次包检查 |
| 核实固件交付、处理不确定更新、保存验收证据 | `mosaico-release-evidence` | [发布证据](.agents/skills/mosaico-release-evidence/SKILL.md) | 历史有限情景检查；本次包检查 |

按当前任务选用，不要求一次加载全部技能。历史情景检查不是跨项目有效性证明，也不能代替当前项目的硬件验证。

## 容易混淆的边界

- **多机状态设计**回答“拆开、换位、改主意之后，用户应该看到和听到什么”；**设备协作**检查实际传感器、通信、状态和时钟是否实现这些规则。已有产品规则不重新审批；只有任务同时涉及两层时才分别读取。
- **界面迭代**处理视觉和操作体验；**界面还原**处理批准后的原型到 C/GSP 的实现一致性。
- **体验原型**中的声音参考用于比较感知变化；深入合成、PCM 或声学问题时再选**音频验证**。

Echoy 的音乐规则、设备数量、磁阈值、既往安装许可和未验收的三机修复，都不是新项目的默认要求或通用成功结论。

## 使用与安装

1. 按上表让 agent 读取所需 `SKILL.md`，并按需读取同目录的 `references/`。无需先安装全部技能。
2. 要在其他项目启用时，将选中的**完整技能目录**复制到目标项目的 `.agents/skills/`，保留其参考文件。遇到同名目录先比较，不直接覆盖。
3. 目录位置保持原样：5 项工程技能在 `.agents/skills/`；3 项试用技能在 `skills/`，本次不将它们移动到自动发现目录。实际发现和调用方式以客户端为准。

本包不修改全局配置、不安装依赖、不自动调用设备，也不授予烧录、录音或远程写入权限。目标项目规则、已有授权、固定工具版本和官方设备流程始终适用。

分组入口：[工程技能](.agents/skills/README.md) · [体验与设计试用技能](skills/README.md)。

## 检查和证据

在本分支根目录，用 Python 3.9 或更新版本运行，无第三方依赖：

```sh
python -m unittest discover -s tests -v
```

检查覆盖 8 项技能的目录、名称、描述、目录内引用和统一入口，以及试用技能的元数据和评估案例结构。检查器只支持本包使用的格式，不是完整 YAML 或技能规范认证。

- [本次整理与验证记录](docs/skills-consolidation.md)：范围、检查和分支安排。
- [5 项工程技能的历史验证](.agents/skills/VALIDATION.md)：有限情景应用和结构检查。
- [3 项试用技能的历史验证](docs/mosaico-skills-verification.md)：包检查；独立行为对照未执行。
- [待执行的行为评估](evals/mosaico-experience/README.md)：案例和评分办法，不是已通过的测试。

## 分支维护

两组技能统一在 `skills/mosaico-development` 维护，原 `skills/mosaico-reuse-v0.1.0` 的提交完整保留在其历史中；不另建旧版 tag。仅在统一分支推送并核验包含原提交后，删除重复分支。此次整理不修改产品分支、产品 tag 或 GitHub 默认分支设置。
