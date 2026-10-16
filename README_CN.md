# Echoy for Mosaico

[English](README.md) · 简体中文

在 Mosaico 上敲出节奏，水平拼接设备，让不同声部回应彼此。

**[下载固件](https://github.com/MacChu0315/Echoy/releases/tag/echoy-three-basic-ba951963754a) · [安装说明（英文）](docs/installation.md) · [开发文档（英文）](docs/development.md)**

<p>
  <img src="docs/screenshots/v1.0.0-connected.png" width="280" alt="左侧 Mosaico 已连接，保留录制的乐句">
  <img src="docs/screenshots/v1.0.0-following.png" width="280" alt="右侧 Mosaico 跟随左侧，有独立播放按钮">
</p>

真机截图：两台已连接，均处于暂停状态。[图片来源](docs/screenshots/README.md)。

> **使用前请注意：** v1.0.0 仍需启动磁校准，开机或重启时请将设备**分开至少 15 cm，放在非金属桌面上**。**三台运行中换位后，移动的设备可能无法重新跟随。** [已知问题详情（英文）](docs/releases/v1.0.0.md#what-was-accepted-and-what-was-not)。

## 三步开始玩

1. **录一段。** 点“录制”，等待“预备 3、2、1”，再在四拍录制时间内敲击。乐句会自动试听一次，然后停止。记录的是触摸时机，不是麦克风声音。
2. **拼起来。** 把另一台同朝向贴到右侧，它会跟随左侧乐句；拼接不会自动开启持续播放。
3. **加入声部。** 选择脉冲、声线、微尘或和声，在希望发声的设备上分别点“播放”。暂停只影响本机；拆开后，本次运行中的乐句和播放／暂停状态会保留。

最多支持三台同朝向水平排列。重启会清空录制的乐句，并重置音色选择。[安装与使用细节（英文）](docs/installation.md)。

## 应该用哪个版本？

下载 **v1.0.0** 可以使用留存的三机版本。它是**有已知限制的预发布版本**，并未通过完整三机验收。

**`master` 现在以发布的三机版本为代码基线，顶部保留后续文档修改。** 要取得发布时的确切源码快照，请使用 [`echoy-three-basic-ba951963754a`](https://github.com/MacChu0315/Echoy/tree/echoy-three-basic-ba951963754a) tag。

[版本、源码 tag 与校验值（英文）](docs/releases/v1.0.0.md) · [已验收双机源码](https://github.com/MacChu0315/Echoy/tree/echoy-dual-accepted-20261014)

## 开发与复用

Echoy 使用共享 C 音乐逻辑和原生 ESP-GSP 界面。[开发文档（英文）](docs/development.md)包含环境准备、有声桌面模拟器、构建和测试方法。

独立的 [Mosaico Skills 分支](https://github.com/MacChu0315/Echoy/tree/skills/mosaico-development)提供 8 项可复用的工程与体验设计技能，并分别注明验证范围。

## 许可证与维护

Echoy 自有代码与文档采用 [MIT](LICENSE)，版权归 © 2026 MacChu0315。第三方组件遵循各自许可证；**ESP-GSP 仅限与 Espressif 产品一起使用**。再分发时请保留[第三方声明](THIRD_PARTY_NOTICES.md)和随固件提供的许可材料。

Echoy 作为一个可运行的实验项目留存，目前暂无持续更新或长期支持计划。
