# Echoy for Mosaico

[English](README.md) · 简体中文

在 Mosaico 上敲出节奏，水平拼接设备，让不同声部回应彼此。

**[下载 v1.1.0 固件](https://github.com/MacChu0315/Echoy/releases/tag/v1.1.0) · [安装说明（英文）](docs/installation.md) · [开发文档（英文）](docs/development.md)**

<p>
  <img src="docs/screenshots/v1.0.0-connected.png" width="280" alt="左侧 Mosaico 已连接，保留录制的乐句">
  <img src="docs/screenshots/v1.0.0-following.png" width="280" alt="右侧 Mosaico 跟随左侧，有独立播放按钮">
</p>

真机截图：两台已连接，均处于暂停状态。[图片来源](docs/screenshots/README.md)。

> **v1.1.0：** 包含三机换位修复、共用 Flash 校准、录制后的本机提前回应和随角色变化的中央试奏，支持拼接状态下开机。[版本详情（英文）](docs/releases/v1.1.0.md)。

## 三步开始玩

1. **录一段。** 点“录制”，等待“预备 3、2、1”，再在四拍录制时间内敲击。乐句会自动试听一次，然后停止。记录的是触摸时机，不是麦克风声音。
2. **拼起来。** 把另一台同朝向贴到右侧，它会跟随左侧乐句；拼接不会自动开启持续播放。
3. **加入声部。** 选择脉冲、声线、微尘或和声，在希望发声的设备上分别点“播放”。暂停只影响本机；拆开后，本次运行中的乐句和播放／暂停状态会保留。

最多支持三台同朝向水平排列。重启会清空录制的乐句，并重置音色选择。[安装与使用细节（英文）](docs/installation.md)。

## 应该用哪个版本？

请下载 [v1.1.0](https://github.com/MacChu0315/Echoy/releases/tag/v1.1.0)，并为所有设备安装同一份固件。发布页附有校验文件和许可材料；详见[安装说明（英文）](docs/installation.md)和[版本说明（英文）](docs/releases/v1.1.0.md)。

本版本包含验收通过的三机修复、玩法说明和中央试奏。旧的 [`echoy-three-basic-ba951963754a`](https://github.com/MacChu0315/Echoy/tree/echoy-three-basic-ba951963754a) 和[已验收双机](https://github.com/MacChu0315/Echoy/tree/echoy-dual-accepted-20261014) tag 保持不变。v1.0.0 的校验值与限制保留在[历史版本记录（英文）](docs/releases/v1.0.0.md)。

## 开发与复用

Echoy 使用共享 C 音乐逻辑和原生 ESP-GSP 界面。[开发文档（英文）](docs/development.md)包含环境准备、有声桌面模拟器、构建和测试方法。

独立的 [Mosaico Skills 分支](https://github.com/MacChu0315/Echoy/tree/skills/mosaico-development)提供 8 项可复用的工程与体验设计技能，并分别注明验证范围。

## 许可证与维护

Echoy 自有代码与文档采用 [MIT](LICENSE)，版权归 © 2026 MacChu0315。第三方组件遵循各自许可证；**ESP-GSP 仅限与 Espressif 产品一起使用**。再分发时请保留[第三方声明](THIRD_PARTY_NOTICES.md)和随固件提供的许可材料。

Echoy 作为一个可运行的实验项目留存，目前暂无持续更新或长期支持计划。
