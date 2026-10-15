# Echoy — reusable Mosaico development skills

This repository currently distributes five portable development skills derived
from Echoy. It contains skills and usage/validation documentation only; application
source history, firmware packages and device records are not included.

Start with the [skill index](.agents/skills/README.md) for selection and reuse.
Copy selected complete folders into another project’s `.agents/skills/`.
The guides cover GSP parity, runtime performance, peer cooperation, audio
validation and release evidence. They do not install tools or grant device-write
permissions.

See the [validation record](.agents/skills/VALIDATION.md) for the checks performed
and their limits. Target-project rules, pinned versions and official device
workflows continue to apply.

## 体验与设计技能（试用版）

本分支另收录三项从 Echoy 讨论与原型复核提炼的技能：体验原型、界面迭代、多机与异步状态设计。见 [中文索引与使用说明](skills/README.md)。

新增技能暂存于 `skills/`，不覆盖上述五项工程向技能，也不自动安装。格式检查已执行；独立 agent 的有／无 skill 行为对照未执行。按需试用，不将本包当作已通过跨项目验证的默认方法。
