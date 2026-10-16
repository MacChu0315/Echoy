# Mosaico 体验与设计技能

全部 8 项技能与选择方式见[统一入口](../README.md)，工程技能见[工程分组](../.agents/skills/README.md)。

**技能包 v0.1.0 是试用版，不是 Echoy 产品版本或最终固件归档。** 本包来自已审议的项目讨论与原型复核提炼稿。

| Skill | 使用场景 |
|---|---|
| [体验原型](prototyping-mosaico-experiences/SKILL.md) | 方向筛选、核心体验原型、向真机交接 |
| [界面迭代](iterating-mosaico-ui/SKILL.md) | 保留认可基线，优化布局、视觉和手势 |
| [多机与异步状态](modeling-mosaico-multi-device-interactions/SKILL.md) | 拼接、换位、失联或准备中改主意 |

优先使用前两项；第三项仅按需使用。每个目录独立包含正文和参考，不要求同时加载。正文不依赖 Superpowers、特定 MCP 或固定硬件数值。音频检查放在体验原型的按需参考中。

## 与仓库原有技能的关系

五项工程技能仍在 `.agents/skills/`，与本组统一在 `skills/mosaico-development` 分支维护；本次不修改技能正文。这三项暂存于 `skills/`，不会自动安装或覆盖现有技能。状态设计负责产品语义和用户意图；已有 `mosaico-peer-cooperation` 侧重协作实现／调试，两者不需要每次同时加载。

## 使用

首次试用可让 agent 读取所需 `SKILL.md` 及其引用文件。启用到另一个项目时，把需要的完整目录复制到该项目的 `.agents/skills/`，保留 `references/`，遇到同名目录先比较，不直接覆盖。当前项目规则与设备契约始终优先。

格式依据 [Agent Skills 规范](https://agentskills.io/specification)，Codex 加载方式参见 [官方技能说明](https://learn.chatgpt.com/docs/build-skills)。其他客户端按其加载方式使用。本次不修改你的电脑、全局设置、其他仓库或自动任务。

## 检查与限制

在仓库根目录，用 Python 3.9 或更新版本的标准库运行：

```sh
python -m unittest discover -s tests -v
```

检查文件组织、约定格式、内部引用、自包含性和评估案例结构，无第三方依赖。**独立 agent 的有／无 skill 行为对照未执行。** 包检查通过不代表任务成功率提升，也不代表产品 UI、固件或硬件验收通过。

见 [验证记录](../docs/mosaico-skills-verification.md) 和 [行为评估方法](../evals/mosaico-experience/README.md)。完整聊天、旧原型与最终工程未复制进本分支。本次结构检查已覆盖全部 8 项技能，见[整理验证记录](../docs/skills-consolidation.md)；历史 v0.1.0 检查仅覆盖本组 3 项，独立行为对照仍未执行。
