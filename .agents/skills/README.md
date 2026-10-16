# Mosaico 工程技能

这 5 项技能侧重实现、调试和交付。全部 8 项技能的选择与安装方式见[统一入口](../../README.md)；体验与设计试用技能见[另一个分组](../../skills/README.md)。每项可独立复制使用，无须同时加载。

| Skill | 适用任务 | 边界 |
|---|---|---|
| [mosaico-gsp-parity](mosaico-gsp-parity/SKILL.md) | 批准原型到 C/GSP、生成资源和原生输入一致性 | 不代替初始视觉设计或浏览器样式工作 |
| [mosaico-runtime-performance](mosaico-runtime-performance/SKILL.md) | 触摸卡顿、音频超时、并发与内部内存 | 测完整路径；主机测试不证明真机时序 |
| [mosaico-peer-cooperation](mosaico-peer-cooperation/SKILL.md) | 实现或排查物理接触、通信、成员关系、时钟与权限 | 产品语义和支持的物理拓扑由目标项目决定 |
| [mosaico-audio-validation](mosaico-audio-validation/SKILL.md) | 参考算法、实际 PCM 和实体声音 | 数字正确性与听感验收分开 |
| [mosaico-release-evidence](mosaico-release-evidence/SKILL.md) | 固件身份、不确定更新和验收记录 | 不授予设备或远程写入权限 |

`mosaico-peer-cooperation` 负责“如何可靠实现已确定行为”；试用的 `modeling-mosaico-multi-device-interactions` 负责“用户操作应影响谁、何时生效、失败时保留什么”。不要把实现检查当成产品规则，也不用每次同时读取两项。

## 使用

复制所需技能的完整目录到目标项目 `.agents/skills/`，保留 `references/`。例如让 agent 使用 `mosaico-runtime-performance` 分析音频播放时的触摸卡顿，并提供目标项目的日志、源码和版本。客户端支持显式技能调用时，可使用其对应语法；否则直接提供 `SKILL.md` 路径。

本组补充现有 Mosaico UI、模拟器、设备操作与构建指南，不复制或要求安装它们。命令、工具版本、公开接口与权限以目标项目为准；不携带 Echoy 的设备身份、生产调参或历史写入授权。发布技能提供可选的[验收记录模板](mosaico-release-evidence/references/acceptance-record.md)，可以省略不适用字段。

## 验证状态

[历史记录](VALIDATION.md)包含有限的独立情景应用、负例选择和结构检查，不是统计基准或跨项目有效性证明。本次整理保留技能正文与参考不变，新增整个技能包的结构检查，详见[本次验证](../../docs/skills-consolidation.md)。

这些方法源于 Echoy 的实现与实物反馈；来源提交与验证边界保留在 Git 历史及上述记录中。后续某个产品修复尚未验收，不会因本包整理而变成已验证的通用经验。
