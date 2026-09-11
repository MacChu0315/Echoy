# Echoy：后续 agent 交接

更新日期：2026-09-12。用户已将工程位置确定为独立的 `Echoy/`。Tasks 1–7 已完成：原型 oracle、独立固件骨架、音乐/会话/DSP/UI，以及真实 BSP 接触/无线适配、Echoy 协议和漂移时钟已建立。Task 8 起继续设置、电量、诊断、镜像和控制层接线。

## 工作位置与阅读顺序

唯一工程根目录：`/home/zhuzhengming/esp/ts/Mosaico/Echoy`。

1. 阅读当前任务要求及实际作用于 Echoy 的 `AGENTS.md`（若存在）。
2. 阅读 [复刻规格](docs/superpowers/specs/2026-09-11-echoy-mosaico-design.md)。
3. 按 [实施计划](docs/superpowers/plans/2026-09-11-echoy-mosaico.md) 的 10 个任务实施；A 节定义新目录下的依赖与命令位置。

用户要求实施时持续推进到适用验证完成；若当前请求仅为分析/交接维护，则完成该请求后停止，不把本文件视为新增实施授权。

```text
/home/zhuzhengming/esp/ts/Mosaico/
  Echoy/                    本工程；源码、测试、工具、文档和输出都在这里
    HANDOFF.md              本文件
    docs/superpowers/       已有规格和计划
    output/playwright/      已有 6 张原型参考截图
  esp-mosaico-bsp/           同级 BSP 依赖及硬件示例
  esp-mosaico-vibe/          hello_world、Recovery 组件、mosaico.py 与 utils
  esp-mosaico-claw/          工厂固件经验参考，不导入其运行时
```

`CMakeLists.txt`、`main/`、`components/`、`tests/`、`tools/`、`pc/` 后续直接建在 Echoy 根目录内；不要再套一层应用目录。保留用户已移动的文档及截图。Vibe 的 AGENTS 不跨目录作用于 Echoy；使用其工具时参考环境、Recovery 和设备操作要求，但不能用其中的应用目录约定覆盖用户的明确位置要求。

## 原型与实现要求

- 源 HTML：`/home/zhuzhengming/esp/Sync/Echoy_v5.0.3.html`。
- SHA-256：`d23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5`。
- 文件名是 v5.0.3，实际内容标注 5.0.4；以该文件内容和哈希为准。
- 480×480、中英文、4 角色×3 乐句、96 BPM、D 小调、16 步/小节、1–3 台。
- 推荐方案：Vibe hello_world 模板思想 + LVGL 9 + 可移植 C 核心 + 本地 DSP + BSP 适配；真实声音由各台 Mosaico 输出。
- 完整保留演奏页、角色/乐句页、状态/错误页、音量弹层以及四角色动态图形与触摸逻辑。
- 发现不入场，吸合后才对拍；加入不自动播放；拆开解除回应但保留共同拍子；只回应直接左邻的完整 motif。暂停、音量 0 或输出故障不得删除有效 motif。
- 原 HTML 的桌面、外壳拖拽、收纳区、玩法指引和故障面板保留在电脑端工具，不缩进设备产品屏幕。

## 依赖、构建与设备操作

从 Echoy 根目录读取 `../esp-mosaico-bsp` 和 `../esp-mosaico-vibe/projects/hello_world`。不要复制原模板的相对路径后直接构建；计划 A 节列出了 manifest、Recovery 组件及 CMake include 的具体新路径。

- BSP 使用同级仓库，核对固定版本；不要同时依赖 Vibe 子模块内第二份 BSP。
- ESP-Iris/utils 位于 `../esp-mosaico-vibe/submodule/esp-mosaico-utils`；目前尚未初始化。实施时只准备所需的固定版本依赖，不更新参考仓库版本来掩盖问题。
- 依赖要求 ESP-IDF >=6.2 / ESP32-S31。前期检查未找到兼容环境，实施时重查实际环境；本次未安装 SDK。
- 新应用创建后，在 Echoy 根目录运行 `idf.py build`；PC 构建使用 `cmake -S pc -B build-host`。这些是后续命令，当前缺少应用代码，不能视为已经运行通过。
- 设备工具入口为 `python ../esp-mosaico-vibe/mosaico.py`，并非 Echoy 内自带脚本。先准备 utils、读取 CLI 源码及 `--help`，确认它支持 Echoy 的外部绝对路径；当前尚未核实这一点。
- 必须保留 Recovery 分区、`esp_mosaico_app_recovery`、`CONFIG_ESP_IRIS_OTA_DEFAULT_VIA_RECOVERY=y` 和 `iris_ota_support_start()`。不绕过正常工具直接 flash，不为了工具路径限制把项目搬回 Vibe。

## 当前证据与尚未验证部分

前期已检查原 JS 的 1,200 个音乐组合及独立开机/吸合/拆开保拍/无自动播放路径，并通过 Playwright 查看 UI 和保存截图。截图是现场参考图，含时间状态和截图取整误差；正式视觉测试需重新生成冻结状态的 480×480 基准。详细记录见规格及计划末尾。

当前已实现启动骨架、音乐/会话/DSP/UI 和真实 BSP radio/contact 适配。Task 7 全套 CTest 24/24、wire/clock/session/state 10/10、ASan/UBSan 24/24；三实例延迟/丢包/重排/分区/重启覆盖，12 小时非整数漂移模拟的存储边界误差最大 0.882 us。真实三机无线/起音尚未测；可信成员 UID/boot 与 2+1 组交接必须由 Task 8 控制层接线。Task 6 的 74/74 视觉对照继续通过。镜像 3,154,848 bytes，仅可通过 Recovery 安装到 `ota_0`；1 条 factory 尺寸 warning 保留。

结构更新时 Echoy 与外层 Mosaico 均未初始化 Git，三个参考仓库工作区干净。初始化 Echoy Git 前按用户规则取得许可；没有 Git 不阻止已授权的本地实现/验证。不要在参考仓库建立 Echoy 工作分支或提交 Echoy 文件。任何建议的 commit 都以 Echoy 已获准初始化为前提。

## 可直接交给后续 agent 的提示词

> 请在 `/home/zhuzhengming/esp/ts/Mosaico/Echoy` 实施 Echoy，先阅读该目录的 `HANDOFF.md`、`docs/superpowers/specs/2026-09-11-echoy-mosaico-design.md` 和 `docs/superpowers/plans/2026-09-11-echoy-mosaico.md`。以指定 HTML 及其哈希为复刻基准，按 10 个任务完成真实固件、完整 UI、音乐逻辑和验证。工程根目录已经由用户确定，源码、测试、工具、文档及输出全部放在 Echoy；同级三个仓库只作为参考和依赖。遵循 Echoy 作用域规则，参考 Vibe 的环境和 Recovery 工作流，但不要把工程移回 Vibe。先适配 CMake/manifest 相对路径，并核实 mosaico.py 对外部工程路径的支持。保留 Recovery；不导入 ESP-Claw runtime；不能用静态 UI 或浏览器发声替代真机实现。特别验证四角色十二乐句、直接左邻 motif、拆开保拍以及暂停/输出故障仍保留 motif。硬件条件不足时继续不受影响的本地工作，明确报告未验证部分。初始化 Git 前按用户规则取得许可，其余已授权本地实现可继续；不 push/创建 PR，设备写入依据当前会话授权执行。
