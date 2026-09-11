# Echoy for Mosaico Implementation Plan

> **For agentic workers:** Use `superpowers:executing-plans` to implement this plan task-by-task after the user starts implementation. Steps use checkbox syntax for tracking. 本文件当前已按用户指定的独立 Echoy 目录更新；读取交接文件本身不代表实施、远程写入或部署授权。用户要求开始实施后，按已有授权推进，无需让用户再次选择内部执行方式。

**Goal:** 将指定 Echoy HTML 的四角色十二乐句、全部产品界面、单机演奏、三机回应/对拍和异常路径移植为可运行的 Mosaico 应用，并保留完整电脑参考与验收工具。

**Architecture:** 参考同级 Vibe 的 hello_world，在 `/home/zhuzhengming/esp/ts/Mosaico/Echoy` 直接建立独立 ESP-IDF 工程，使用 LVGL 9 和 BSP。可移植 C 核心负责音乐、状态和逻辑会话，ESP-IDF 适配负责音频样本调度、物理接触、无线及 ESP-Iris；PC 使用同一核心和同一 LVGL 视图。

**Tech Stack:** C11、ESP-IDF >=6.2 / ESP32-S31、LVGL 9、BSP / esp_codec_dev / ESP-NOW、ESP-Iris；Node.js 运行原型对照，Python 标准库承载本机测试桥，浏览器用于参考截图和测试操作。

**Spec:** [复刻规格](../specs/2026-09-11-echoy-mosaico-design.md)。先读 [HANDOFF.md](../../../HANDOFF.md)、规格和作用于 Echoy 的 AGENTS；`../esp-mosaico-vibe/AGENTS.md`（相对工程根目录）仅作为参考仓库工具/组件工作流的依据，不能覆盖用户指定的独立工程位置。

## Global Constraints

- 唯一基准 `/home/zhuzhengming/esp/Sync/Echoy_v5.0.3.html`，SHA-256 `d23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5`；实际页面版本 5.0.4。
- 固定 480×480；默认中文并完整支持英文；4 角色×3 乐句；96 BPM、D 小调、16 步/小节；1–3 台设备。
- 无新生产依赖、云服务、账号、摄像头/麦克风功能、AI runtime；确需新增时依用户规则单独处理。
- Recovery 分区契约保留，普通应用 `CONFIG_ESP_IRIS_OTA_DEFAULT_VIA_RECOVERY=y`，必须调用 `iris_ota_support_start()`；不能直接 `idf.py flash`。
- 原型演示时间值与实际硬件性能严格区分；原 JS 是行为对照，不是无线同步或声学质量证据。
- 拆开保拍；发现不入场；入场不自动播放；只有直接左邻 motif 决定回应；本机静音/输出失败不删除 motif。
- 应用源码、工具、测试、文档和输出均放在 Echoy；三个同级仓库提供参考和已有依赖，不在其中开发 Echoy，不更新子模块版本以规避错误，不修改 factory 模板。
- 本地成果保留；远程 push/PR/部署授权不由本计划授予。安装前依用户当时授权和项目规则执行。

## A. 执行起点与证据

除文档间 Markdown 链接外，本文相对路径及命令默认以 `/home/zhuzhengming/esp/ts/Mosaico/Echoy` 为根；`main/`、`components/`、`tools/`、`tests/`、`docs/` 均直接位于 Echoy。`BSP/` 指同级 `../esp-mosaico-bsp/`，模板位于 `../esp-mosaico-vibe/projects/hello_world/`。这是用户明确指定的工作区布局，正式构建应记录依赖位置与提交，并在依赖缺失/版本不符时清楚报错。

第一条项目修改前执行：

```sh
cd /home/zhuzhengming/esp/ts/Mosaico/Echoy
pwd
git rev-parse --show-toplevel
git -C ../esp-mosaico-vibe status --short --branch
git -C ../esp-mosaico-vibe submodule status
git -C ../esp-mosaico-bsp status --short --branch
git -C ../esp-mosaico-claw status --short --branch
```

本次核实时 Echoy 尚未初始化 Git，所以上面的 `git rev-parse` 预期失败。先确认实际状态，不因此停止已授权的文件实现和验证；若要初始化 Git/创建首个提交，按用户规则询问是否在 Echoy 初始化，不能擅自执行 git init。已有 Git 时再检查 Echoy 自己的 status/log/remote；若位于默认分支，首次提交前创建合适工作分支。本文所有“建议提交”均以 Echoy 已获准初始化 Git 为前提，不在 Vibe 创建工作分支或提交 Echoy 成果。

后续实施需要准备 Vibe 固定版本的 utils 依赖（本轮交接更新不执行）：

```sh
git -C ../esp-mosaico-vibe submodule update --init submodule/esp-mosaico-utils
```

只递归初始化 utils 明确需要的子模块。同级 BSP 已存在，不同时引入 Vibe 内第二份 BSP。按参考工作流验证 ESP-IDF 路径、版本、Python、esp32s31；前期已检查的主机安装未满足 >=6.2，实施时重查。需要安装时读取 `../esp-mosaico-vibe/skills/espressif-env-setup/SKILL.md`；开始构建时读取 `../esp-mosaico-vibe/skills/idf-low-noise-build/SKILL.md`，日志留在 Echoy 输出目录。

设备工具保持在 Vibe，Echoy 根目录当前没有 `mosaico.py`。utils 准备后先运行 `python ../esp-mosaico-vibe/mosaico.py --help` 和 `install --help`，读取 CLI 对 `--project` 的路径解析、工作区发现和构建位置实现。候选目标是 Echoy 的绝对路径；外部工程支持尚未验证，不能直接把该命令描述成已可安装。如果 CLI 强制限制工程在 Vibe 内，保留 Echoy 位置，先完成不受影响的构建/测试，再报告具体限制并提出本地适配方案；不得移动工程、在 Vibe 下创建重复工程或绕过 Recovery 直接 flash。

### 独立工程的依赖路径

下表是后续 Task 2 必须适配的路径，不是本轮已创建的构建配置：

| 引用位置 | 路径/处理 |
| --- | --- |
| `main/idf_component.yml` 的 BSP override_path | `../../esp-mosaico-bsp/components/esp-mosaico-bsp` |
| `main/idf_component.yml` 的 esp_iris override_path | `../../esp-mosaico-vibe/submodule/esp-mosaico-utils/ESP-Iris/components/esp_iris` |
| 根 CMakeLists.txt 的 EXTRA_COMPONENT_DIRS | `${CMAKE_CURRENT_LIST_DIR}/../esp-mosaico-vibe/components/esp_mosaico_app_recovery` |
| 根 CMakeLists.txt 的 system_update include | `${CMAKE_CURRENT_LIST_DIR}/../esp-mosaico-vibe/cmake/system_update.cmake` |
| 磁吸/网络组件 | 按实际依赖声明引用同级 BSP 的对应组件，不把整个三个参考仓库当组件目录 |

YAML override_path 相对声明它的 manifest；如果写入 `components/<name>/idf_component.yml`，必须按新的文件深度重新计算。CMake 路径以定义处为准；直接 include Vibe 原脚本可保留其 `CMAKE_CURRENT_LIST_DIR` 指向的 tools/utils 位置，复制该脚本到 Echoy 则会破坏这些路径。CMake、manifest、Recovery 与工具选用的 BSP/utils 版本必须一致。

## B. 代码与资源布局

以下是新建文件，不要求修改三个参考仓库已有实现。实现时允许把短小、紧密相关的静态函数保留在同一文件，不无意义地拆分。

```text
Echoy/                                  当前工程根目录，已存在
  HANDOFF.md                            当前交接入口，已存在
  CMakeLists.txt, sdkconfig.defaults, sdkconfig.application.defaults, partitions.csv
  main/main.c, main/CMakeLists.txt, main/idf_component.yml, main/Kconfig.projbuild
  components/echoy_core/
    include/echoy_types.h, echoy_core.h, echoy_music.h, echoy_wire.h, echoy_clock.h
    core.c, session.c, phrase.c, response.c, harmony.c, wire.c, clock.c
    CMakeLists.txt
  components/echoy_audio/
    include/echoy_audio.h, include/echoy_dsp.h
    dsp.c, voices.c, filters.c, scheduler.c, codec_output.c, CMakeLists.txt
  components/echoy_ui/
    include/echoy_ui.h, ui.c, perform.c, role.c, status.c, volume.c
    instruments.c, i18n.c, theme.h, fonts/, CMakeLists.txt, idf_component.yml
  components/echoy_platform/
    include/echoy_platform.h, contact.c, radio.c, storage.c, battery.c
    diagnostics.c, screen_mirror.c, CMakeLists.txt
  reference/Echoy_v5.0.3.html, reference/manifest.json
  assets/fonts/                         字体输入与许可证
  tools/extract_reference.mjs, tools/generate_fonts.py
  tests/oracle.mjs, tests/cases/, tests/fixtures/, tests/compare_oracle.py
  tests/test_music.c, test_state.c, test_clock.c, test_wire.c, test_dsp.c
  tests/CMakeLists.txt, tests/ui_cases.json, tests/visual_compare.py
  pc/CMakeLists.txt, core_runner.c, ui_runner.c, platform_pc.c
  README.md
  tools/echoy-sim/
    run.py, bridge.py, scenarios.py, web/index.html, web/adapter.js, web/audio-worklet.js
  docs/echoy/
    reference.md, protocol.md, hardware-acceptance.md
  docs/superpowers/                      已有规格和计划，原地更新
  output/playwright/                     已有参考截图，保留
```

`reference/` 原文件只作为本地测试资产，提交前确认归属/许可；未经批准不上传外部服务。正式源码和工具注释按仓库现有英文风格；中文产品文案与原型一致。本计划是中文交接说明。

## C. 共用接口与时序约定

以下是待创建的应用接口，不是 BSP 已有 API。先写头文件和测试，再填实现。opaque 类型的内存大小由组件实现统一给出，PC 与 MCU 不能各写一份结构定义。

```c
#define ECHOY_MAX_MEMBERS 3
#define ECHOY_STEPS_PER_BAR 16
#define ECHOY_MAX_EVENTS 16
#define ECHOY_MAX_CHORD_NOTES 3

typedef uint64_t echoy_uid_t;
typedef enum { ECHOY_PULSE, ECHOY_VOICE, ECHOY_DUST, ECHOY_HARMONY } echoy_role_t;
typedef enum { ECHOY_KICK, ECHOY_RIM, ECHOY_HAT, ECHOY_TONE,
               ECHOY_GRAIN, ECHOY_CHORD } echoy_kind_t;
typedef enum { ECHOY_ART_NONE, ECHOY_ART_SHORT,
               ECHOY_ART_BROKEN, ECHOY_ART_ANSWER } echoy_articulation_t;
typedef struct {
    uint8_t step, note, note_count, notes[ECHOY_MAX_CHORD_NOTES];
    uint8_t harmony_count, harmony[ECHOY_MAX_CHORD_NOTES], chord_id;
    echoy_kind_t kind;
    echoy_articulation_t articulation;
    bool accent;
    double velocity, duration_s, expression, max_duration_s;
} echoy_note_t;
typedef struct { uint8_t count; echoy_note_t notes[ECHOY_MAX_EVENTS]; } echoy_motif_t;
typedef struct {
    echoy_role_t role;
    uint8_t phrase; /* 1..3 */
    double committed_activity;
} echoy_voice_spec_t;

/* source == NULL 表示独奏；source 存在但为空时按 JS 对照处理。 */
bool echoy_music_compose(const echoy_voice_spec_t *self,
                        const echoy_motif_t *source, uint32_t bar,
                        uint8_t duplicate, double bpm, echoy_motif_t *out);

typedef struct echoy_core echoy_core_t;
typedef struct echoy_event echoy_event_t;
typedef struct echoy_snapshot echoy_snapshot_t;
typedef struct echoy_bar_plan echoy_bar_plan_t;
bool echoy_core_dispatch(echoy_core_t *, const echoy_event_t *, uint64_t now_us);
void echoy_core_tick(echoy_core_t *, uint64_t now_us);
void echoy_core_snapshot(const echoy_core_t *, echoy_snapshot_t *);
bool echoy_core_bar_plan(const echoy_core_t *, uint32_t bar, echoy_bar_plan_t *);

typedef struct echoy_ui echoy_ui_t;
typedef void (*echoy_ui_emit_fn)(const echoy_event_t *, void *);
void echoy_ui_render(echoy_ui_t *, const echoy_snapshot_t *, uint64_t now_us);

typedef struct echoy_dsp echoy_dsp_t;
void echoy_dsp_render(echoy_dsp_t *, int16_t *stereo, size_t frames,
                     uint64_t first_sample_index);
```

`echoy_event_t` 用带 tag 的 union，事件至少包含：play/pause、role request/ready/failed/cancel、phrase、activity、volume、contact connected/uncertain/released、peer heartbeat、sync proposal/commit、data fault/output fault/recovered、power、battery、locale。调试注入也走同一入口；UI 不直接改设备结构。

`echoy_snapshot_t` 按原型提供：当前/候选角色与乐句、角色记忆、activity/committedActivity、volume、电量及有效性、lifecycle、transport、输出/数据错误、motifAvailable、成员与 boot、session epoch/generation、已生效/待生效边、同步 stage、feedback 的文本 key/参数/进度种类。生成 UI 反馈时按 `DuijuApp.feedback()` 的优先级，不让低优先级的“已生效”覆盖同步失败。

`echoy_bar_plan_t` 包含 session generation、state revision、bar index、epoch/rate 及每成员 motif/output-enabled。发布后不可变；下一个小节提交完新状态才生成该小节事件。音频持有已发布快照，不读正在修改的角色数据。

## Task 1: 固定原型，并建立可运行的规则对照

**Files:** `reference/*`、`tools/extract_reference.mjs`、`tests/oracle.mjs`、`tests/cases/*`、`docs/echoy/reference.md`。

**Consumes:** 原 HTML 和规格中的哈希。**Produces:** 可复现的 JS 期望事件、状态轨迹、完整页面/动作清单。

- [x] 校验哈希，把原文件作为只读参考保存；manifest 记录输入路径、哈希、内嵌版本与三个参考仓库提交。
- [x] 按模块注释边界提取 `DuijuResponse`、`DuijuHarmony`、`DuijuEngine`、`DuijuSession`，保留原文。提取失败/重复匹配必须报错，不用脆弱的固定行号静默截断。
- [x] 建立 Node VM 对照。下面代码展示确实可用的原型入口；持久工具应加输入哈希验证和机器可读输出。

```js
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const html = fs.readFileSync('reference/Echoy_v5.0.3.html', 'utf8');
const begin = html.indexOf('/* Complete-phrase response');
const end = html.indexOf('/* Simulator only:', begin);
assert(begin >= 0 && end > begin);
const sandbox = vm.createContext({});
vm.runInContext(html.slice(begin, end), sandbox);
const engine = new sandbox.DuijuEngine();
const notes = engine.phrase(engine.devices.A, null, 0, 0);
assert(notes.some(n => n.kind === 'kick' && n.step === 0));
const session = new sandbox.DuijuSession(0);
session.tick(1);
assert.notEqual(session.devices.A.sessionId, session.devices.B.sessionId);
```

- [x] 对角色×乐句×活动量×小节×左邻建立 fixtures。活动量覆盖 0、1、默认值和 .25/.26/.30/.42/.52/.65/.70/.72/.74/.75/.76/.78/.80/.82 的左右邻近值；重复角色 duplicate=0/1/2。覆盖空 motif、全弱音、长音、和弦、多音相同落点。
- [x] 固定注入时钟、字体和动画，采集全部产品状态的中英文基准；记录界面动作和真实计算样式。动态谱点必须与该帧 motif 相符。
- [x] 验证命令：`node tests/oracle.mjs --self-test`。预期哈希匹配、原型 self-test 通过、生成 fixture 数量明确；生成两次输出应相同。
- [x] 本任务只保存原型与测试基础；建议提交 `test: capture Echoy reference behavior and UI baselines`。

## Task 2: 应用模板、显示和恢复入口

**Files:** 项目根构建/配置文件、`main/*`、`components/echoy_ui/ui.c`、`pc/CMakeLists.txt`、`tests/CMakeLists.txt`。

**Consumes:** 固定版本 BSP/utils、hello_world。**Produces:** 正常应用启动、480×480 显示和 ESP-Iris 恢复入口；PC 可编译测试目标。

- [x] 参考 `../esp-mosaico-vibe/projects/hello_world`，在当前 Echoy 根目录创建应用文件，保留现有 docs/output/HANDOFF.md；逐项对照 partitions.csv、sdkconfig.application.defaults、system_update.cmake 和 main/CMakeLists.txt，不直接把 GSP/claw 分区复制过来。
- [x] 新 manifest 使用 `idf: ">=6.2"`、`lvgl/lvgl: ">=9,<10"` 和 A 节独立工程依赖路径；不能照抄 hello_world 原有相对子模块路径。记录实际解析版本。组件依赖使用已有 BSP 组件，禁止新增一个 ESP-NOW 管理器与 BSP 争用无线。
- [x] 检查 utils 提供的恢复和屏幕后端接口。确保 ESP-Iris 在 UI/音频初始化失败时仍可进入 Recovery；参考调用骨架如下，不凭空设计 ESP-Iris API：

```c
ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
/* 初始化 NVS；遇到错误不得直接擦除 sysmeta/整片 flash。 */
iris_ota_support_start();
lv_display_t *display = bsp_display_start();
/* display == NULL 时记录错误并保留已启动的恢复服务。 */
```

- [x] 对照 hello_world 实际启动顺序与 utils 初始化前提调整以上顺序；任何错误都必须在诊断中可见。不要将错误全转成日志后仍报告应用健康。
- [x] 在 Echoy 根目录激活已确认环境后执行 `idf.py --preview set-target esp32s31`、`idf.py build`；按低噪声构建技能保留日志。检查镜像大小与 ota_0 容量。
- [x] PC 先编译 `core_runner` 和测试框架，使用项目 CMake/CTest，不引入新的生产测试框架。
- [x] 预期：构建通过、分区契约未变、应用启动入口不依赖 claw。此时尚不能声称 UI 或音乐完成。建议提交 `feat: add Echoy application with retained recovery support`。

## Task 3: 四角色十二乐句的 C 实现

**Files:** `echoy_types.h`、`echoy_music.h`、`phrase.c`、`response.c`、`harmony.c`、`test_music.c`、`core_runner.c`、`compare_oracle.py`。

**Consumes:** Task 1 fixtures。**Produces:** `echoy_music_compose()`，与 JS 同输入同输出。

- [x] 先实现输入/输出 runner，给 12 个默认独奏 fixture 写失败用例。runner 读取固定格式 case，输出标准化 JSON；不要让脚本自己重算期望结果。
- [x] 对照原 `DuijuEngine.phrase/soloVariant` 移植独奏；保持原排序和去重后截断顺序。使用 double 先保证语义，DSP 再用 float；不要在规则移植时一并定点化。
- [x] 再移植 `DuijuResponse.analyze/compose`：onset/release/held/space、preceding、slot resize、弱音权重、pitch 与事件数量上限逐项对应。
- [x] 移植 `DuijuHarmony`：六个 CHORDS、pitchEvidence、brokenPositions、shortPlan、answerPlan、shortVoicings、时长 deadline。预计算有界 voicing 候选表，生成器和输入一起版本化；不得手选几个和弦替代搜索评分。
- [x] JS 数学语义显式实现并测试，例如：

```c
static int wrap16(int n) { return ((n % 16) + 16) % 16; }
static double js_round(double x) { return floor(x + 0.5); }
/* 返回有限数值前先检查输入，避免 NaN 越过数组边界。 */
```

- [x] 扩大到 Task 1 全矩阵，并测试脉冲→声线→和声、和声→微尘→脉冲、三台相同角色、调换左右。只改右邻时左邻 motif 必须逐字段不变；音量不进入 compose 输入。
- [x] 比较字段：step/kind/note/notes/harmony/chord/articulation/accent 完全相等；vel/dur/expression/maxDuration 绝对误差 <=1e-6。诊断打印首个差异对应的输入、角色、小节和事件序号。
- [x] 验证：`cmake -S pc -B build-host`；`cmake --build build-host`；`ctest --test-dir build-host --output-on-failure -R music`；`python3 tests/compare_oracle.py --runner build-host/echoy_core_runner`。
- [x] 差异未清零不能继续把该核心当基准。建议提交 `feat: port Echoy phrase and harmony composition rules`。

## Task 4: 本机状态与三台逻辑会话

**Files:** `echoy_core.h`、`core.c`、`session.c`、`test_state.c`、`tests/cases/session-*`。

**Consumes:** Task 3，规格第 6–7 节。**Produces:** dispatch/tick/snapshot/bar_plan，以及可重放的状态轨迹。

- [x] 用纯 C 假时钟测试事件：play 后 waiting、625ms 拍边界开始、连续点击取消 waiting；pause 不改 motif；关机清除 pending、boot generation 改变后拒绝旧 ready 回调。
- [x] 将角色准备与角色提交分开；ready/failed 携操作 generation，取消后迟到 ready 不得生效。换角失败只清除该操作，旧角色、乐句和演奏不变。
- [x] 原型时间对照使用微秒整数：450000 接触确认、1050000 模拟同步准备、1800000 保持。实际硬件将 SYNC_READY/ROLE_READY 作为事件，不能 sleep 固定时间直接成功。
- [x] 实现“会话全暂停立即提交”与“部分暂停等下一小节”的区别。音量立即生效；表达立即反馈/着色，committedActivity 到边界才更新。
- [x] 将 output fault 与 motif availability 拆开；给下列轨迹写断言：

```text
A→B 正在演奏；A output_fault：B.source 仍为 A，B.motif 不变。
A data_fault：当前已提交小节的来源保留；下一边界 B 变独奏。
A data_recovered：在下一边界恢复来源，不在小节中间重新编曲。
暂停 A：A 输出为空，但 B 仍回应 A；暂停 B 不影响 A。
```

- [x] 逻辑会话测试：独立启动发现不入场；A>B 确认才合并；加入 C 不自动播放；拆边后 session 仍相同；交换左右边在小节生效；失联保持后回到连续本地时间；原参考掉电后幸存成员换参考；拒绝环/重复边/第四成员。
- [x] 移植 `DuijuApp.feedback()` 的状态优先级到 UI 无关的消息 key；加载/错误优先于正常演奏页，音量关闭不清 pending。
- [x] 验证：CTest `-R state` 和 JS/C 状态轨迹对照。真机差异（真实准备完成、心跳、设备身份）用显式适配事件测试，不修改 JS 来掩盖差异。
- [x] 建议提交 `feat: implement Echoy state and performance sessions`。

## Task 5: 本地音频合成和按样本调度

**Files:** `echoy_audio/*`、`test_dsp.c`、`tests/fixtures/audio-*`、PC WAV 输出适配。

**Consumes:** 不可变 bar_plan 与实时 activity/volume。**Produces:** PCM/WAV 和真实 codec 输出，不通过浏览器代真机发声。

- [x] DSP 先在 PC 渲染单事件 WAV，测试所有采样有限、输出有界、预计时刻前静音、包络结束后有界收尾；随机噪声用原种子 1187 / Park–Miller 生成器。
- [x] 逐项移植 `DuijuSynth.note()` 的 kick/rim/hat/tone/grain/chord；保留多谐波、滤波器频率变化、和弦音数归一化、short/broken/answer 区别。声音事件 kind 不能统一映射为正弦波。
- [x] 实现 0.3125s delay、.18 feedback、低通阻尼及各台独立尾音/音量；禁止暂停一台时清空其他台的 DSP。浏览器总 limiter 的跨台作用不能逐样本重现于分布式扬声器；用单台参考及实际听测判断差异。
- [x] 以固定 voice pool 和预分配音频缓冲运行，初始池 64 个逻辑音符声部；每个声部可含多个振荡器。用最大密度长音/延迟场景测峰值，不允许靠无声丢音满足实时性；容量不足先定位并调整有界资源。
- [x] 调度目标样本由 `round((target_session_time - output_epoch) * sample_rate)` 推导；精确到音频块内。初始块 256 frames，队列 4 块，记录实际 DMA 深度。同步误差评估要扣除已排队音频的延迟，不能用 callback 时间当起音时间。
- [x] 实时 activity 按原 DuijuAudio 的持续时长比例修正，只影响未开始音符；volume/mute 平滑更新，取消已排队旧 clockVersion 事件；错过事件丢弃并计数，不回放积压。
- [x] 接 ES8311，打开 48kHz/16-bit/2ch；检查每次 codec 返回值。短暂输出故障只进入输出故障状态，重新打开失败要保留重试提示；不把扬声器故障上报成数据丢失。
- [ ] 验证：CTest `-R dsp`、12 乐句离线 WAV、Web Audio 单台参考 A/B 对比；记录 onset、包络、RMS/峰值、频谱差异与听测。不得以压缩器/采样率不同导致的波形差异声称所有音色等价。（主机 DSP/WAV/Web Audio 指标已通过；真实扬声器听测留 Task 10。）
- [x] 建议提交 `feat: synthesize Echoy voices on the local audio output`。

## Task 6: 产品 UI、触摸与语言完整复刻

**Files:** `echoy_ui/*`、`assets/fonts/*`、字体工具、`pc/ui_runner.c`、`tests/ui_cases.json`、`visual_compare.py`。

**Consumes:** Task 1 基准、Task 4 快照和事件、Task 5 hit/sample 时间。**Produces:** MCU 与 PC 共用 LVGL 页面及真实触控事件。

- [x] 固定中英文字体和许可证，从原型所有显示路径提取字形（含动态反馈、Canvas 小字和数字）。字体由源文件生成，不能手改生成 C。缺字检查必须覆盖中文粗体、和声标题及英文长提示。
- [x] 先实现 perform 布局和四种图形：Pulse 同心圆/重音刻度、Voice 七条曲线/音符、Dust 135 个确定性颗粒、Harmony 三条声部带/实际音符时长条；底部 16 步谱点来自 bar_plan。
- [x] 相对拖动公式 `clamp(start + (start_y-current_y)/180, 0, 1)`；直接刻度公式 `clamp((338-current_y)/150, 0, 1)`。pointer cancel/end 必须结束拖动态，不把手指离开控件视为音量变化。
- [x] 写 role 页四选项×三个乐句、返回、当前/候选选中态；写 status 页全部状态和取消/重试；写 volume 弹层遮罩与焦点/触摸隔离。
- [x] 立即触摸反馈与延迟提交分开：100ms 内改变按压/等待反馈；真正音乐切换等拍点/小节。图形频率优先 30fps；暂停保留淡化静态乐句，不清空成占位图。
- [x] UI 不在每次调度 tick 重建所有对象；拖动时更新已有控件。LVGL 锁只在 UI 线程路径使用，文件/NVS/无线和 codec 操作不得位于锁内。
- [x] 把 `EchoyI18n` 的产品字符串及动态拼接逐条转为 key/参数；英文布局按原覆盖规则，切语言不能重置角色、播放或会话。
- [x] PC renderer 对每个 ui_case 输出 480×480 PPM；冻结 clock、activity、motif、hit、locale；转为 PNG 后与浏览器截图对齐，比较 RGB565 量化与字形抗锯齿的预期误差。仅动画/硬件不可视角落可预先定义 mask，不能把不一致的整个页面遮掉。
- [x] 逐个测试控件命中边缘、快速返回、拖动超出屏幕、弹层关闭、低电文字和长英文；核心布局边界偏差 <=2px、无裁切/缺字/触摸穿透。截图差分给出数值和差异图，并人工核对，不只写“相似”。
- [x] 验证：CTest `-R ui`；`python3 tests/visual_compare.py --cases tests/ui_cases.json --renderer build-host/echoy_ui_runner`；真机 UI 性能留到 Task 10。
- [x] 建议提交 `feat: reproduce Echoy product screens and touch interactions`。

## Task 7: 物理接触、演奏协议和真实时钟

**Files:** `echoy_platform/contact.c, radio.c`、`echoy_core/wire.c, clock.c`、对应头文件与测试、`docs/echoy/protocol.md`。

**Consumes:** Task 4、BSP magnetic_interaction_demo 与网络实现。**Produces:** 正确的无线对端、独立于物理边的逻辑成员通信、可测量时间映射。

- [x] 先核对 `mosaico_network_start`、`mosaico_network_set_contact`、`mosaico_peer_link_get_device_id/get_boot_id` 和 raw callback；只初始化一次无线。GAME_EVENT payload 采用 Echoy magic/version，回调复制消息与收到时刻，不阻塞处理。
- [x] 磁场路径复用示例中 `bsp_magnetometer_read` → `mosaico_mag_classifier_process` → presence tracker。明确 invalid/saturated/calibration failure 对应 uncertain，不当成“已确认接触”。使用实际 local_edge/peer_edge 与 UID，拒绝不支持的方向/环/多新邻居歧义。
- [x] 新协议采用显式小端序编码，不直接发送 C struct。建议固定 40-byte 应用头，body<=60，总长<=100，头字段如下：

| Offset | 字节 | 字段 |
| --- | --- | --- |
| 0 | 2 | magic=0x4543 |
| 2 | 1 | version=1 |
| 3 | 1 | kind |
| 4 | 4 | sender_boot |
| 8 | 4 | sequence |
| 12 | 8 | session_creator_uid |
| 20 | 4 | session_creator_boot |
| 24 | 4 | session_nonce |
| 28 | 4 | transaction_id |
| 32 | 4 | apply_bar |
| 36 | 1 | fragment_index |
| 37 | 1 | fragment_count |
| 38 | 1 | body_length |
| 39 | 1 | flags |

- [x] `source_id` 来自外层 BSP 消息并与预期成员匹配。定义 HELLO/TIME_REQ/TIME_RESP/HEARTBEAT/PREPARE/READY/COMMIT/ACK/ABORT；每种 body 编解码、长度与允许状态在 protocol.md 写全并测试。发现消息不授予 membership。
- [x] 状态事务包含 member UID/boot、role/phrase/committedActivity、motifAvailable、transport（只传播观测/本机请求）、有向边、epoch/rate、algorithm version、state revision 和内容摘要；音量不影响其他台编曲。最多 16 片/事务、总数据<=960 bytes、每来源最多 2 个重组事务，超额拒绝并报告，不无限分配。
- [x] 初版先同步参数/状态并在每台计算完整链；不要把 JSON 乐句直接塞入 64-byte network 应用消息。重复/乱序/长度错误/不认识版本/旧 boot/旧事务/截断/摘要错误用 parser 测试拒绝。
- [x] 用四时间戳交换，A 发 t1，B 收 t2/发 t3，A 收 t4：offset=((t2-t1)+(t3-t4))/2，RTT=(t4-t1)-(t3-t2)。至少 8 轮有效交换后，从低 RTT 样本估计偏移；持续采样拟合漂移。记录 timestamp 所在软件层及估计不确定度，不能称其为硬件 PHY 时间戳。
- [x] 先在 20s 窗口用偏移随时间的线性拟合估计 rate；拒绝倒退/异常 RTT 样本。未满足偏移稳定性和计划提前量时留在 syncing；真机同步预算以起音实测决定，不用演示 1.05s 替代。
- [x] 合并按最大成员数/UID 决定时间参考。PREPARE/READY 后约定至少两个小节后的 commit 边界，并重复发送 COMMIT 收 ACK；报文携 state revision，心跳暴露已提交 revision。任何晚于边界的准备不能临时追赶提交。
- [x] 最终 COMMIT 丢失/参考在边界前掉电必须有测试：未收到完整提交的成员维持旧状态，检测版本分歧后提示/降级/重新协商；不宣称在任意分区下仍能保证所有节点原子提交。
- [x] 应用层心跳初值 250ms，连续 3 次缺失判定失联，之后按 1.8s 保持窗口处理；这些是待真机验证的协议默认值，写入配置和诊断，区别于 BSP 的 2.5s 接触会话 stale timeout。
- [x] 测试移除物理边但保留无线后，Echoy GAME_EVENT 仍能同步；只广播 HELLO 的陌生设备不能加入；两个独立演奏组靠近不误并场。真实无线失联、参考重启、旧包重放都不能自动播放设备。
- [ ] 验证：CTest `-R 'wire|clock|session'`；三实例假无线注入延迟、丢包、重排、重复、分区及重启；然后双机/三机测量。建议提交 `feat: synchronize Echoy sessions over Mosaico peer links`。（主机与三实例验证已通过；当前只有一台设备，双机/三机测量留 Task 10。）

## Task 8: 设备设置、电量和可观测性

**Files:** `echoy_platform/storage.c, battery.c, diagnostics.c, screen_mirror.c`、`main/Kconfig.projbuild`、`docs/echoy/hardware-acceptance.md`。

**Consumes:** Task 4 快照、真实 BSP/ESP-Iris 接口。**Produces:** 设置恢复、真实电量、错误与计时诊断、屏幕截图。

- [x] 应用 NVS 命名空间 `echoy`，schema_version=1；保存已提交 role/phrase、各角色表达/乐句记忆、volume、locale。保存前范围校验；新版本不可读时报告设置错误，不清 sysmeta。不保存会话、播放状态、临时 pending 或 boot 身份。
- [x] NVS 写入在输入完成/参数提交后去抖，关闭重启仍默认 paused；角色默认 Pulse，可在本机角色页切换。三台演示身份别名由电脑配置映射，不更改真实 UID。
- [x] 电量服务读 `state_of_charge`；<15 提示、读失败保留有效旧值并诊断。0% 的关停流程根据 BSP power API 验证后实现；不从字符串“0”推断所有传感失败都应关机。
- [x] 原型 IMU/模块诊断通过本地测试注入与可用的真实识别数据呈现。只有现有 manager 提供无副作用的识别才启用；不为读取 EEPROM 直接抢占 slot，不启动摄像头。
- [x] 从已初始化 utils 读取实际 ESP-Iris 扩展入口，注册只读快照和开发构建的测试事件注入；如果没有适合 RPC，使用仓库认可的结构化诊断途径，不能发明 ESP-Iris API。测试注入在普通构建关闭。
- [x] 屏幕镜像应从完整 LVGL 帧/受控捕获取得一致快照；参考 gsp_hello 的按需缓冲生命周期，但不能原样调用 GSP 类型操作 LVGL。打开/关闭/断连后内存恢复，音频不中断。
- [x] 诊断字段：无线 UID/boot 与 Iris 身份映射、session/membership、active/pending edges、state revision、clock offset/rate/uncertainty、sync stage、音频队列/欠载/声部峰值、堆/任务水位、掉包与拒绝原因。不得记录 pairing token。
- [x] 验证持久化范围、坏 schema、NVS 失败、传感失败、镜像反复开关、测试入口发布构建关闭；建议提交 `feat: add Echoy device state and diagnostics`。

## Task 9: 完整电脑验收工具

**Files:** `tools/echoy-sim/*`、`pc/core_runner.c, ui_runner.c, platform_pc.c`。

**Consumes:** 同一 C 核心、同一 LVGL 页面、Task 1 原 HTML。**Produces:** 原型参考模式和固件行为模式，支持所有桌面交互和诊断。

- [x] 参考模式直接提供固定哈希原 HTML；测试服务只绑定 127.0.0.1，只提供明确的项目测试资产，不暴露用户 Sync 整个目录。
- [x] 固件模式启动三个 C 实例，Python 标准库桥管理假时钟和消息传送。桌面坐标只进入空间适配层，转换为 contact 事件；核心输入不得含桌面 x/y。
- [x] 保留外壳拖动、吸附预览、碰撞/取消、滚动、收纳/开机、第三台、全部六步指引及重开、速查、中英切换、故障面板、事件日志、HTML→固件对照表。指引只观察，不代用户播放或重排。
- [x] 固件模式的 .screen 显示同一 LVGL renderer 帧并转发屏内触点；不能继续展示 JS 图形却标成 MCU UI。PC 的音频来自 DSP PCM，经本地桥/AudioWorklet 或 PC 输出适配播放；离线 WAV 用于自动校验。
- [x] 桥消息采用有界逐行请求/响应，Python 解析 JSON 后转换为 C runner 固定事件；一次响应携 snapshot/revision，屏幕和音频与该版本绑定。桥连接断开后测试模式停止注入，实际真机继续自主演奏。
- [x] 启动入口定义为 `python3 tools/echoy-sim/run.py --mode reference`、`--mode firmware`、`--headless --scenario all`；这三个命令是本任务要实现的工具接口，并非当前已有命令。
- [x] 场景覆盖：素材 loading 及取消、下次 role fail、output fault、data fault、sync fail、磁吸 uncertain、Wi-Fi off/peer online、peer offline、低电/零电、仅一台/全部缺席、第三台加入、重置、故障解除不改摆放/播放意图。
- [x] 真机模式不能靠桌面坐标伪造实际磁吸证据，故障注入只能显式开发模式；参考模式浏览器后台暂停规则继续保留。
- [x] 验证全场景 headless 结果和浏览器键鼠/触摸路径；建议提交 `test: add Echoy firmware simulator and fault scenarios`。

## Task 10: 真机闭环与交接

**Files:** 最终修复、README、`docs/echoy/hardware-acceptance.md`；原始日志/截图/WAV 放在本地输出目录，清理敏感信息后再决定哪些纳入版本控制。

**Consumes:** Tasks 1–9。**Produces:** 经过适用验证的工程与真实完成状态。

- [ ] PC 完整执行 CTest、JS/C fixture 对照、全部模拟场景、视觉对照；运行 `git diff --check`，检查生成字体/资产可重新生成。
- [ ] 在 Echoy 根目录重新完整构建 `idf.py build`。没有兼容 SDK/依赖时继续完成可执行的主机验证，明确列出构建阻塞，不能标记固件完成。
- [ ] 完成 A 节外部工程路径核实且已有所需设备安装授权后，用 `python ../esp-mosaico-vibe/mosaico.py list` 取得实时设备；空白/未核实设备先使用该工具的 `recover`。正常安装候选命令为 `python ../esp-mosaico-vibe/mosaico.py install --project /home/zhuzhengming/esp/ts/Mosaico/Echoy`，仅在确认 CLI 接受该路径且选中正确设备后执行；不执行 system-update/分区变化来绕开本计划。
- [ ] 核实每台同一 Device ID 经正常→Recovery→正常且 Boot ID 更新，Recovery ready，Echoy 健康；不能把上传成功或设备重连当作完成。
- [ ] 单机：4×3 乐句、0/默认/100% 表达、0/70/100% 音量、全部状态页、取消/重试、中英、不播放启动、字体可读性、84px 触控、屏幕方向。
- [ ] 双机：分别演奏不自动对拍；吸合对拍；交换左右实际听到回应变化；拆开持续共同拍子；静音/输出故障仍供 motif；数据失败/恢复只在边界改变来源；模拟参考掉电、断连、重启旧包。
- [ ] 三机：A→B→C、C→B→A、重复角色、Harmony 每种变体、第三台加入不自动播放、两侧快速拆装；同时首次吸合歧义必须明确显示待确认，分次吸合应能恢复。
- [ ] 连续演奏 30 分钟，同时重复触摸和打开屏幕镜像；记录音频 underrun、最大声部、任务/堆水位、UI 帧率、触摸延迟、无线同步分布。不凭观察“听起来差不多”替代数据。
- [ ] 声学测试使用多通道同步录音或已校准的共同采集设备测同一事件起音；固定摆位并处理声传播差，报告 P50/P95/max 和时间漂移。目标 P95<=10ms/max<=20ms；仪器不足时标记未验证，不能拿软件日志相位作替代结论。
- [ ] 检查原型需求清单逐条映射：每项必须有实现文件、自动测试/手动验收、证据路径及通过/受限状态。真实扬声器低频与噪声颗粒效果需听测，不承诺浏览器 PCM 与扬声器声学信号逐样本相同。
- [ ] README 写清环境、构建、PC 运行、设备安装、三台演示设置和已知限制；只更新真实、需要跨会话保留的信息。不要为了流程创建额外状态文件。
- [ ] 检查 Git 实际差异，仅提交本任务文件；建议末次提交 `test: verify Echoy on Mosaico hardware`，只有真机验证实际完成才用该说明。未完项目不得伪装为通过。

## D. 验收索引

| 需求 | 对应任务 | 关键证据 |
| --- | --- | --- |
| 四角色/十二乐句/重复角色/左邻回应 | 1、3 | JS/C 全字段对照 |
| 当前与待生效设置、播放/取消、输出与数据区分 | 4 | 可重放事件序列 |
| 本地音色、尾音、包络、音量、样本调度 | 5、10 | WAV 与实际录音/欠载记录 |
| 全部页面、图形、触摸、语言 | 1、6、10 | 冻结时刻截图、触控/真机照片 |
| 磁吸/对端身份/会话/拆开保拍 | 4、7、10 | 三节点测试与真机日志/录音 |
| 电量、持久化、诊断、恢复 | 2、8、10 | NVS/故障测试、Iris 操作记录 |
| 原 HTML 屏外全部内容 | 9 | 桌面交互、指引与故障场景 |

## E. 给下一位模型的执行提示

可以直接将以下文字交给后续模型：

> 请先读取 `/home/zhuzhengming/esp/ts/Mosaico/Echoy/HANDOFF.md`，再按照其中链接的实施计划和规格实施 Echoy。唯一工程根目录是 `/home/zhuzhengming/esp/ts/Mosaico/Echoy`，源码、测试、工具及文档都放在该目录内；同级三个仓库只提供参考和依赖。遵循 Echoy 作用域内的规则，不因 Vibe 的目录约定改变用户指定的位置。校验原 HTML 哈希，按计划适配独立工程的 CMake/manifest 路径，并核实 Vibe 的 mosaico.py 支持外部工程路径后再安装。保留 Recovery，不导入 ESP-Claw runtime。逐任务实现并验证四角色十二乐句、完整 UI、直接左邻 motif、拆开保拍、暂停/扬声器失败仍保留 motif，以及真实本地合成。硬件不足时只暂停相关验收，继续本地工作并报告未验证部分。Echoy 尚未初始化 Git，初始化/首次提交前按用户规则取得许可，其余已授权本地实现可继续。保留本地成果，不 push/创建 PR，设备写入依据当前会话的明确授权执行。

## F. 前期原型分析验证记录

- `git status --short --branch` / `git remote -v` / `git rev-parse HEAD`：三个参考仓库状态、来源和版本已核对；未修改其工作区。
- `sha256sum /home/zhuzhengming/esp/Sync/Echoy_v5.0.3.html`：与计划记录相符。
- Node VM 检查 1,200 个音乐组合，累计 4,892 个事件：step、duration、maxDuration、事件数边界通过；独立启动/吸合/拆开保拍/不自动播放通过。此检查是原型合法性检查，不是尚未存在的 C 实现测试。
- 本地 Playwright：实际打开页面，检查 computed styles，操作角色/返回/音量/Harmony/语言并保存 6 张截图。唯一控制台错误为 favicon.ico 的 404。
- 尚未完成：ESP-IDF >=6.2 环境、utils 子模块 API 核对、固件代码/构建、DSP 听测、设备安装、真实无线/声学测试。这些均属于后续实施工作，未宣称通过。
