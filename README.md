# mosaico-dev-skills

本库由现有 `MacChu0315/Echoy` 仓库的 `skills/mosaico-development` 分支分发。Echoy 产品代码仍由 `master` 维护；本分支只保留技能库材料。

从 Echoy 与 Peek 开发中提炼的 Mosaico 共用技能库。包含 **6 项工程技能和 3 项体验／设计试用技能**；不包含产品源码、固件、设备身份、实测录音或既往设备操作授权。

## 按任务选用

| 当前任务 | 技能 | 状态 |
| --- | --- | --- |
| 把认可原型落实到共享 C／原生 GSP，检查画面和操作一致性 | [mosaico-gsp-parity](.agents/skills/mosaico-gsp-parity/SKILL.md) | 原工程技能；补充截图提交、素材与有效配置检查 |
| 排查绘制卡顿、实时任务等待、帧率和内存 | [mosaico-runtime-performance](.agents/skills/mosaico-runtime-performance/SKILL.md) | 原工程技能；补充精确缓存和实际负载统计 |
| 接入实时传感器、处理新样本、回放与角色时序 | [mosaico-realtime-input](.agents/skills/mosaico-realtime-input/SKILL.md) | 本次新增；有限离线情景检查 |
| 实现或排查接触、无线、成员关系和时钟 | [mosaico-peer-cooperation](.agents/skills/mosaico-peer-cooperation/SKILL.md) | 原文保留 |
| 验证参考合成、PCM 与实体声音 | [mosaico-audio-validation](.agents/skills/mosaico-audio-validation/SKILL.md) | 原文保留 |
| 固件身份、官方更新与审阅交接 | [mosaico-release-evidence](.agents/skills/mosaico-release-evidence/SKILL.md) | 原工程技能；补充 ZIP 和未达目标的停点 |
| 探索产品方向、制作核心体验与真机交接 | [prototyping-mosaico-experiences](skills/prototyping-mosaico-experiences/SKILL.md) | experimental；原行为评估未执行 |
| 保留认可基线，优化布局、视觉和手势 | [iterating-mosaico-ui](skills/iterating-mosaico-ui/SKILL.md) | experimental；原行为评估未执行 |
| 定义拼接、换位、取消与异步提交的体验 | [modeling-mosaico-multi-device-interactions](skills/modeling-mosaico-multi-device-interactions/SKILL.md) | experimental；原行为评估未执行 |

只读取当前任务需要的技能及其按需参考。产品语义、硬件事实、依赖版本、资源预算与有效授权来自目标工程，不从 Echoy 或 Peek 直接继承。经验是工程判断依据，不是跨项目验收结论。

## 本次补充的经验

- 新完整样本先进入处理／行为逻辑，再进入可覆盖的显示队列；读失败不能重复提交旧数组。
- 采集、接收、逻辑时钟、动画刷新分别定义；过期证据先结算，重复帧、暂停与重绘不刷新样本时间。
- 回放实际数组和原始故障时间线，驱动同一份生产 C；坐标只能用于对照。
- 区分瓦片回调、GSP 完整帧和真屏 FPS；睁眼跟踪与闭眼静态分别计数，普通预览与截图／轮询分开测量。
- 保留认可画面。精确缓存验证全部依赖、边界像素与分配失败；降细节需要对应的视觉决定，不能扩大一次授权。
- 普通素材构建只读，明确更新源素材的入口；测试应到达预期失败条件，不被无关库版本提前挡住。
- 检查生成后的有效配置，不能只看 defaults。截图等帧提交；录像保留实际时间间隔。
- 安装源码、构建产物、文档提交和设备运行版本分别记录；“先这样”不等于未达指标已经通过。

## 在工程中使用

当前源目录保持 Echoy 技能分支的结构：`.agents/skills/` 放工程技能，`skills/` 放手动选用的 3 项试用技能。旧试用元数据、参考与评估案例原样保留。

作为 submodule 的接入方式见 [本地接入和远程发布准备](docs/INTEGRATION.md)。工程中的 `.agents/skills/<name>` 可以指向库内对应的完整技能目录，避免保留两份可独立修改的正文；`references/` 必须一起可读。客户端是否自动发现需单独核实，也可以直接给 agent `SKILL.md` 路径。不要自动启用全部试用技能或修改全局配置。

也可独立复制选定的完整技能目录，同时保留许可和来源说明。遇到同名内容先比较，不覆盖其他项目的定制版本。

## 检查

Python 3.9 或更新版本，无新增生产依赖：

```sh
python3 -m unittest discover -s tests -v
```

检查技能目录／名称、相对引用、独立复制后的参考、自包含性、试用状态与评估记录结构。可用 `skill-creator` 的 `quick_validate.py` 另查技能元数据；它不属于本库运行依赖。

[本轮验证](docs/VALIDATION.md)区分结构测试、有限 agent 情景应用和实际 submodule 克隆检查；不将其冒充固件、设备或一般成功率验证。原试用评估仍见 [旧评估材料](evals/mosaico-experience/README.md)。

## 来源与维护

[来源与许可](docs/SOURCES.md)记录 Echoy 技能分支及 Peek 交接提交，保留原历史验证说明。新的独立库不复制来源仓库 `.git` 或产品历史。

本次将独立本地库 `mosaico-dev-skills` 的 `8958188` 内容导入原有技能分支，保留旧分支历史，并调整分发与接入说明。使用现有仓库，不另建远程仓库或新发布分支。

```sh
git clone --single-branch --branch skills/mosaico-development https://github.com/MacChu0315/Echoy.git mosaico-dev-skills
cd mosaico-dev-skills
python3 -m unittest discover -s tests -v
```

作为 submodule 消费时，远程 URL 使用 `Echoy.git` 并选择本技能分支，固定它实际发布的 commit；不能继续引用未发布的独立本地库 commit。详见 [接入与分发](docs/INTEGRATION.md)。
