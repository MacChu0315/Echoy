# Echoy：后续 agent 交接

更新日期：2026-09-12。唯一工程根目录为 `/home/zhuzhengming/esp/ts/Mosaico/Echoy`。Tasks 1–9 已实现；Task 10 的完整主机验证、固件构建与文档已完成，真机安装/闭环仍受 Recovery 身份验证失败阻塞。不要把主机结果或烧录成功写成完整真机复刻。

## 先读

1. 当前任务及作用域内 `AGENTS.md`（若存在）。
2. [README](README.md)：当前环境、构建、PC 运行、设备安装及三台演示。
3. [逐项需求与证据](docs/echoy/requirements-matrix.md)。
4. [真机验收与当前阻塞](docs/echoy/hardware-acceptance.md)。
5. 原始[规格](docs/superpowers/specs/2026-09-11-echoy-mosaico-design.md)和[实施计划](docs/superpowers/plans/2026-09-11-echoy-mosaico.md)；其中“尚无源码/未初始化/未找到 SDK”等是规划时的历史记录，以本文件和 README 当前记录为准。

## 固定约束

- 源 HTML 为 `/home/zhuzhengming/esp/Sync/Echoy_v5.0.3.html`，SHA-256 `d23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5`；页面内容版本 5.0.4。
- 480×480、中英文、四角色十二乐句、96 BPM、D 小调、16 步/小节、最多三台。
- 默认 Pulse/中文/暂停，音量 .70。保存已提交设置，不保存 playing、会话、启动身份、待提交事务。
- 发现不入场，入场不自动播放；只回应直接左邻完整 motif；拆开解除回应但保拍；暂停、音量 0、输出失败不删除有效 motif。
- `echoy_control` 单任务写核心/协议/设置；UI/无线回调入有界队列；DSP/codec 有独立 owner；截图不取音频锁。
- 源码、测试、工具、文档和输出都在 Echoy；三个同级仓库只提供参考和固定依赖，不改其源码，不引入 ESP-Claw runtime。

## 已准备的环境

兼容 SDK 已安装在 `/home/zhuzhengming/esp/esp-idf/esp-idf-echoy-v6.2-dev`，固定提交 `c712a0dde385d659a1470a136251980d31a70bc1`，实际版本 6.2.0/esp32s31。低噪声 doctor 的 Git describe 报 6.1-dev 属已记录的版本识别差异；实际 clean/build 和依赖解析成功。Vibe utils 子模块已按 `d832e6623225bd94af67610e2a49d362ade4f000` 初始化；BSP 使用同级 `esp-mosaico-bsp`，不使用第二份副本。

Echoy 已初始化 Git，当前本地工作分支为 `feat/echoy-mosaico`。没有远程写入授权，不 push、不建 PR。不要再把规划文档里的“初始化前询问”当作尚未完成的前置条件。

## 真机当前状态

本轮用户已授权 `/dev/ttyACM0` 恢复/烧录。所有固件写入和 Gateway 生命周期只通过 `python ../esp-mosaico-vibe/mosaico.py`；同一 Gateway 可做只读诊断/镜像及已授权开发验收 RPC，不能直接开 USB/串口或读取 pairing token。

`recover --dry-run` 验证唯一 ESP32-S31 ROM 接口和 reviewed 2.8.5-recovery。随后 `recover` 写入及哈希校验成功，reset 后的 Gateway 身份验证却报 `Recovery endpoint reattached but identity verification did not complete`，最终 exit 4。主机权限下 `/dev/ttyACM0` 随后消失；未取得 Device ID、Boot ID 或 Recovery ready。**没有执行 Echoy install，也没有声称实际发声。** 详细原始证据在 `output/task10/` 和验收文档所列 `.codex-runs/mosaico/.../raw.log`。

下一步是现场确认同一块板的电源、USB 与 RESET，然后实时 `mosaico.py list`。在恢复身份与 ready 前不得盲目再次刷写或向无关串口安装。受限环境会隐藏设备节点：sandbox 的 `ls` 无设备不是主机无设备的充分证据，须通过指定工具和必要的主机权限核实。

保留 factory Recovery 分区与 `CONFIG_ESP_IRIS_OTA_DEFAULT_VIA_RECOVERY=y`。约 3.85 MB 的普通 Echoy 仅能由 Recovery 写入 `ota_0`；通用 `idf.py flash` 给出的 factory 地址不可使用。只有实际完成相同 Device ID 的 normal→Recovery→normal、Boot ID 更新、Recovery ready、Echoy healthy 才能接受设备闭环。

`CONFIG_ECHOY_DEV_INJECTION` 默认关；本轮临时开发镜像仅编译、未安装。方法 5 可在开发版走同一 LVGL 指针处理；显示锁忙时保留取消状态，由控制任务获得锁后先清旧手势，再接收新事件；这不能代替物理触摸传感器测量。普通构建产物在结束前已恢复。后续若安装开发版，验收结束必须恢复普通版并读出 `injection_enabled=false`。

## 最终集成修复

数据故障/loading 及恢复通过标准协议在 apply 小节提交 motif 可用性，左右两端 revision/source/motif 一致；最后邻机失联后的单成员会话保留时钟并恢复本地参数提交；协议 syncing/degraded 与核心快照/UI 提示同步。共享 `echoy_view_update()` 从实际 events 和会话时间调用 `echoy_view_hit()`，覆盖 Pulse 衰减、Voice 音高、旧快照/时钟、暂停和静音。PC 交互时钟改用 `performance.now()` 积累真实 elapsed，单次最多 250ms，PCM 完整保留该区间；工作线程预缓冲 192ms、总缓冲上限 750ms，断连不累积离线时间。这里的 PC 缓冲增加预览延迟，真机声画延迟仍未测量。

最终普通/开发/恢复普通 IDF 构建分别为 3,847,152 / 3,847,792 / 3,847,152 bytes。当前 `build/sdkconfig` 关闭开发注入，普通 ELF 无 `inject_rpc`；factory Recovery 和 `ota_0` 约束保持。新回归及构建证据在 `output/final-fix/`。本次没有重试设备操作。

## 验证边界

当前 CTest 40/40、JS adapter/guide 13/13、headless 29 项、Chrome 23 项通过；74 张参考截图、真实 LVGL 视觉对照、字体及和弦表生成验证通过。GCC ASan/UBSan 40/40 通过；因既有 ptrace 限制关闭 LeakSanitizer。22 组 C/Chromium 音频再生成与测量成功，未做真实听测。主机墙钟耐久完成 1800.006 秒，1,554 组帧、4,662 个指针事件，三个 C 进程正常退出；内存稳定，P95 三帧生成读取 46.75ms。记录单独位于 `output/task10/host-endurance/`，不能当作 MCU 30 分钟验收。

物理屏幕方向/可读性、触摸传感器、实际 ES8311 扬声器、设备堆/任务水位、Iris 镜像争用、磁吸与无线、双/三机同步及校准声学 P95/max 均尚未接受。目标不变：触摸 ≤100ms、UI 约 30fps、声学 P95≤10ms/max≤20ms。只读记录这些限制，不生成虚假录音或硬件数据。
