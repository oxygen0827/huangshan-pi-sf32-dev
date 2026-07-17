# 黄山派 SF32 开发基座

这是一个面向立创黄山派 / LCKFB Huangshan Pi 的独立应用开发仓库。

本仓库的目标不是只做手表。当前代码基于立创例程里的 `lvgl/watch`
路径，因为这条路径已经验证了这块板子的 AMOLED 屏幕、触摸、LVGL、
资源系统、启动器和烧录流程。手表 UI 只是这块板子的一个开发出口；
后续也可以继续开发传感器、音频、存储、USB、低功耗、板级控制或其他
GUI 应用。

## 开发板

- 开发板：立创黄山派 / LCKFB Huangshan Pi
- 目标板型：`sf32lb52-lchspi-ulp`
- 模组：SF32LB52x-MOD-1-N16R8
- 屏幕：CO5300 AMOLED，390x450，QADSPI
- 触摸：FT6146
- 串口：CH340 USB UART
- 本机验证串口：`/dev/cu.usbserial-13220`

## 上游来源

本仓库依赖两个上游项目：

- 官方 SiFli SDK
  - 仓库：https://gitee.com/SiFli/sifli-sdk
  - 分支：`main`
  - 本机验证版本：SDK 2.5.0，build `cbac8e56`
  - 本地路径：`/Users/hushaohong/vibe-coding/huangshan-pi-workspace/sifli-sdk`

- 立创黄山派例程
  - 仓库：https://github.com/OpenSiFli/lckfb-hspi-ulp_example.git
  - 本地路径：`/Users/hushaohong/vibe-coding/huangshan-pi-workspace/lckfb-hspi-ulp_example`

完整 SDK 没有复制进本仓库。它作为外部依赖保留在同一个 workspace
里。当前仓库复制了立创 `lvgl/watch` 的已验证应用结构，用它作为第一版
可运行开发基座。

## 立创例程与传感器验证

2026-07-01 已在 `sf32lb52-lchspi-ulp` 板型和 SDK 2.5.0 上构建并刷板跑过
`lckfb-hspi-ulp_example` 里的 9 个例程：

| 例程 | 板上验证结果 |
| --- | --- |
| `adc` | 输出 VBAT 和 PA34 ADC 读数 |
| `gpio` | PIN20 高低电平切换，KEY2 中断已注册 |
| `uart` | `uart2` 初始化并发送 `uart2 demo`；RX 需要外接 UART 回环/设备 |
| `I2C/charger` | I2C2 扫到 AW32001 `0x49` 并读出 chip ID；原例程会循环写充电电流寄存器 |
| `RT-Device/sensor` | 官方例程会在缺失 LTR303 时断言；见下方传感器实测结论 |
| `ws2812` | `rgbled` 注册成功并进入颜色循环 |
| `lvgl/lvgl_v8_demos` | CO5300、FT6146、littlevgl2rtt 初始化成功 |
| `lvgl/lvgl_v9_demos` | CO5300、FT6146、LVGL v9 初始化成功，并输出 FPS sysmon |
| `lvgl/watch` | SDK 2.5 兼容后 `Main` app 能正常 LOAD/START/ONRESUME |

当前这块板子的 I2C 实测结果：I2C3 上 LSM6DSL 在 `0x6a` 应答，LTR303
`0x29` 和 MMC56X3 `0x30` 未应答。Runtime 因此必须先探测再初始化驱动，
不能直接照搬官方 sensor 例程的无条件初始化路径。


## 当前开发进度

> 2026-07-05：对标 `vibeboard-runtime-gpl` 的详细功能清单、差距和近期路线已整理到
> [docs/vibeboard-runtime-gpl-parity.md](docs/vibeboard-runtime-gpl-parity.md)。


参考 ESP32 那边的整理方式，当前黄山派这条线可以先拆成“已经稳定驱动 /
验证过”的部分，以及“下一阶段要补齐的 App API / 产品化能力”。

### 已经驱动 / 验证过

| 功能 | 状态 |
| --- | --- |
| `lvgl/watch` 启动器、CO5300 AMOLED、FT6146 触摸、LVGL v8/v9 demo | 已跑通，作为当前开发基座 |
| ADC / VBAT、GPIO / KEY2、UART2、AW32001 charger、WS2812 官方例程 | 已逐项验证；UART RX 仍需外接回环或其他设备 |
| Runtime 一次烧录后免重刷 App 安装 | 已跑通；串口 `install_begin/file/end` staging 提交路径已稳定 |
| Runtime App Manager / Launcher API | 已补第一版；串口 / BLE / Web / iOS 可读取当前 App 状态、列出已安装 App、启动、停止和删除已停止 App；板端 UI 已收敛为首页卡片桌面，直接扫描 `/sdcard/apps` 中已安装且可用的 App，支持上下滚动并点击启动 |
| Runtime BLE 安装服务 | 已跑通；Mac 和 iPhone 共用同一条 GATT 分块安装链路 |
| 串口 / BLE 长 JSON 读取 / `json_read` 分块回补 | 已跑通；`capabilities` 等非分页长 JSON 可通过分块回补完整返回；App 列表改为 `apps_page` 小页聚合，Mac / iPhone 共用同一协议边界 |
| Runtime 能力握手 `vb_runtime_capabilities` | 已可供 Mac / iPhone / AI 端直接解析 |
| 板载传感器 JSON `vb_runtime_sensors` | 已驱动；当前实测 LSM6DSL 正常，LTR303 / MMC56X3 缺席时不会再断言，串口 / BLE 自动回归已覆盖 |
| 电源 JSON / Lua helper / power_stage 示例 | 已可用；当前稳定承诺范围是 VBAT + AW32001 只读状态，`power_stage` 已用 manifest `power.battery` / `power.charger` 和 `vibe_power_label(...)` 展示电池与充电状态，串口 / BLE 自动回归已覆盖 JSON |
| 屏幕 Display JSON / Lua helper / display_stage 示例 | 已可用；首版开放 CO5300 面板尺寸/状态/亮度快照和 0-100 亮度设置，暂不开放熄屏/开屏控制，串口 / BLE / iOS 均已接入同一 API |
| RGB JSON / Lua helper `vb_runtime_rgb` / `vibe_rgb(...)` | 已可用；串口 / BLE 自动回归已覆盖设色、读回与恢复 |
| 信息流 `flow_send` / `pc.voice` / flow_stage 示例 | 已跑通；已修复 Lua 清屏后 `flow_label` 悬空导致的回写崩溃，电脑或手机端文本可以稳定回写到板子；App 可用 manifest `flow.*` 或 `vibe_flow_label(...)` 只读展示最新信息流，最近一条会保存到 `/sdcard/apps/.flow` 以便跨复位/跨 BLE 重连恢复，串口 / BLE 独立回归已覆盖 clear/send/status/clear/persist |
| Codex Companion / Codex Pet | 已跑通；板端显示 Codex Desktop 多任务状态、Rocky 宠物动画和额度信息，任务执行、完成、卡住或等待审批时同步更新屏幕与 RGB；左右键切换任务，审批出现时变为 `Allow / Deny`。板端页面不再提供 Talk 入口，语音底层 API 仅为后续兼容保留 |
| 语音桥 `voice_start/stop/status/read/clear` | 已跑通；串口 / BLE 都已完成 16 kHz PCM 短录音、按住说话松手保留、WAV 落盘、回复回写与证据校验 |
| 语音状态 / 受控录音 App API / voice_stage 示例 | 已可用；`vb_runtime_voice` / BLE `voice` / `--voice-only` 提供只读状态，App 可通过 `voice.start` / `voice.clear` action 或 `vibe_voice_start(...)` / `vibe_voice_clear()` 请求 Runtime 限时录音和清空录音，但仍不能直接读取 PCM |
| Runtime manifest + Lua 5.5 VM | 已可用；支持函数、闭包、表、循环、模块和 UTF-8 等完整 Lua 语言能力；标准库、LVGL binding、384 KiB 内存和 50 万指令预算仍由 Runtime 控制 |
| 音频播放 | 已接入 SiFli `audio_server`；支持 App 包内 8-48 kHz、单/双声道、16-bit PCM WAV 异步播放、停止、0-15 音量和状态 JSON |
| GPIO JSON / Lua helper / gpio_keys_stage 示例 | 已可用；当前白名单为 KEY1 / KEY2 只读输入，已完成 Runtime 接线与构建验证 |
| Touch JSON / Lua helper / touch_stage 示例 | 已可用；支持 count/last/event/gesture/delta/duration/active，当前已改为启动即稳定返回可读 JSON，并完成串口 / BLE 真机回归 |
| 串口 / BLE 可靠性回归脚本 | 已可用；安装、abort、冷启动恢复、核心硬件 JSON、`voice-status`、`audio-status` 和真实 `voice` 录音链路已能自动回归 |
| iOS BLE Swift package + Demo App | 已完成 `swift test`、iPhone 工程构建，并对齐 `display()` / `touch()` / `gpio()` 与长 JSON fallback |

### 还没完全沉淀 / 下一阶段目标

| 方向 | 计划 |
| --- | --- |
| App 管理器 / 触摸 Launcher | 第一版命令/API 已接入；板端不再单独提供工程态 App Manager 页面，而是首页直接显示已安装 App 卡片并支持滚动启动；Web/iOS 端继续承担安装、启动、停止、删除和日志提示 |
| 触摸 App API | 第二版高层语义已接入；当前已支持 gesture/delta/duration/active，下一步补拖动连续态和更细的手势语义 |
| GPIO / 板级控制 | 第一版已开放 KEY1 / KEY2 只读白名单；屏幕亮度已作为 display API 独立开放；下一步再评估 LED 和其他板级控制接口 |
| 音频产品化 API | 板载麦克风录音桥和受控 WAV 播放均已打通；板上只有 AW8155 + `SPK` 输出接口，没有扬声器单元，听感验收必须连接外接喇叭 |
| App 包协议与恢复 | 已补第一版 `install_abort`、启动恢复和 manifest 最低校验；下一步继续补重试和更多示例包 |
| BLE / iPhone 真机闭环 | Mac 侧链路已稳定且语音回归通过；下一步补 iPhone 真机连接、安装、GPIO / Touch / 语音回归 |
| 手机 / 桌面工具 | 把当前 Mac / iPhone 的工程型验证工具继续收敛成更稳定的开发者入口 |
| 自动回归覆盖面 | 当前串口 / BLE 已覆盖安装、abort、冷恢复、核心硬件 JSON、`voice-status`、`audio-status` 和真实 `voice` 录音；下一步补真实音频播放、长时、多轮和跨端回归 |
| 传感器边界 | 继续按实测硬件状态推进，不把缺失器件误判成软件故障 |
| 网络能力 | App 包能力模型已明确拒绝 `wifi` / `http` / `network` / `ntp` / `board_ip` 等板载联网声明；天气、AI、云端数据走手机或桌面 bridge 注入；PAN / HTTP App OTA 默认不编译、不导出，只能作为显式实验宏打开的固件级评估 |

当前这条线最核心的工作，已经不是“能不能点亮屏幕”或“能不能装 App”，而是把
这些板级能力继续收敛成一套稳定、可复用、可给 App 直接调用的 Runtime API。

## Codex Companion

`scripts/runtime_apps/codex_pet/` 是运行在黄山派上的 Codex Desktop 状态伴侣。电脑端
Bridge 独占 BLE 连接，Codex Hooks 把任务生命周期事件送入 Bridge，Bridge 再把当前任务
列表、状态、额度和受限审批请求同步到板子。

当前产品边界：

- 板子只负责显示宠物、任务状态、提醒和简单审批；任务输入继续在电脑端 Codex 完成。
- 左右按钮用于切换当前监控到的任务；出现受支持的审批请求时，按钮自动变为
  `Allow / Deny`。
- 命令执行和文件修改审批可以在板端处理；API Key、密码、普通问题和其他敏感输入仍必须
  在电脑端处理。
- 板端页面没有 Talk 按钮。录音、ASR 和语音提交 API 仍保留在 Runtime 中，但不属于当前
  默认交互路径。
- RGB 会随 `connected / running / ready / blocked / needs_input` 状态变化；Bridge 心跳中断
  后会自动回到离线状态，不把旧任务继续显示为运行中。

数据路径如下：

```text
Codex Desktop Hooks
  -> codex_pet_hook.py
  -> codex_pet_bridge.py (single BLE owner)
  -> pet/v1 Flow messages
  -> Huangshan Runtime / codex_pet
  -> Rocky animation + task status + Allow/Deny
```

### 准备 Rocky 宠物资源

仓库不提交从 Codex Desktop 提取的 Rocky 图片资源。`extract_codex_rocky.js` 会在本机读取
已安装的 `/Applications/ChatGPT.app`，校验 ASAR 内资源完整性，并生成 10 帧 VRLE 文件：

```bash
node scripts/extract_codex_rocky.js
node scripts/extract_codex_rocky.js --check
```

VRLE 文件保存在 `scripts/runtime_apps/codex_pet/assets/rocky/`，安装 App 时写入 SD 卡；
运行时一次性解压到 PSRAM。SD 卡只提供资源存储，不会增加 LVGL 运行内存。若资源缺失或
任意一帧校验失败，固件会释放全部动画帧并显示内置矢量宠物，不应出现空圆环。

### 安装到黄山派

先构建并刷入包含原生 Codex Pet helper 的 Runtime 固件：

```bash
./scripts/build.sh
./scripts/flash.sh /dev/cu.usbserial-XXXX
```

首次使用电脑端 BLE Bridge 时准备 Python 环境：

```bash
python3 -m venv .venv
.venv/bin/python -m pip install bleak
```

然后将 App 和 Rocky 资源安装到 SD 卡。黄山派 FinSH 命令行较短，Codex Pet 资源包应使用
64 字节安装块和逐段 UART 节流：

```bash
.venv/bin/python scripts/runtime_install_serial.py /dev/cu.usbserial-XXXX \
  --package-dir scripts/runtime_apps/codex_pet \
  --chunk-bytes 64 \
  --no-echo \
  --command-wait 0.03 \
  --write-chunk-pause 0.003 \
  --final-wait 3 \
  --ready-timeout 90
```

安装过程使用 staging 目录；任一命令失败都会中止提交，不会用半包覆盖当前可运行版本。
安装完成后重新启动板子，串口日志应包含：

```text
[vb_runtime][codex_pet] Rocky loaded: 10 RLE frames in PSRAM
[vb_runtime][lua] codex.pet rc=0
```

### 启动桌面监控

将 [`docs/codex-pet-hooks.json`](docs/codex-pet-hooks.json) 中的项目绝对路径改为当前 clone
路径，并把其中的 Hooks 合并到 Codex 配置。然后启动唯一的 BLE Bridge：

```bash
CODEX_PET_BOARD=<CoreBluetooth peripheral identifier> \
  ./scripts/codex_pet_monitor.command
```

首次使用板端桌面审批时，macOS 会请求辅助功能权限。审批 helper 只接收任务 session ID 和
`approve / deny`，不读取或转发命令参数、文件路径、密码与 API Key。Bridge 运行期间不要再
启动 App Store、语音 Bridge 或其他会直接占用同一黄山派 BLE 连接的服务。

协议、状态归并、安全边界和 MCP 硬件工具详见
[`docs/codex-pet-bridge.md`](docs/codex-pet-bridge.md)。

### 验证与排障

完整本机回归：

```bash
.venv/bin/python scripts/runtime_deep_check.py
```

若屏幕只显示圆环，依次确认：

1. `node scripts/extract_codex_rocky.js --check` 能验证全部 10 帧。
2. SD 卡中存在 `/sdcard/apps/codex_pet/assets/rocky/*.rle`。
3. 冷启动日志出现 `Rocky loaded: 10 RLE frames in PSRAM`；若没有，读取此前的
   `Rocky frame load failed path=...` 日志，不要把 SD 容量误判为运行内存。
4. `codex.pet rc=0` 表示 App 已成功启动；Bridge 连接问题只影响任务状态同步，不会让已经
   加载的宠物图像消失。



### 对标 `vibeboard-runtime-gpl` 的差距和优势

`vibeboard-runtime-gpl` 当前更像一个成熟的 ESP32-S3 Runtime 平台：它有完整
Lua/LVGL 运行时、Wi-Fi/HTTP 安装和 Device Web UI、App 注册/启动器、较多
示例 App，以及 NES/native/gamepad/camera/I2S/网络这类高阶能力。黄山派这边
已经跑通“一次烧录 Runtime + 免重刷安装 App + 串口/BLE/iPhone 同协议管理”，
但距离同等级平台还差下面几块：

| 方向 | 当前差距 | 下一步目标 |
| --- | --- | --- |
| App Manager / Launcher | 已有状态、列表、启动、停止、删除命令；板端桌面直接显示已安装 App 卡片并可滚动启动，Web/iOS 端提供管理器 | 继续打磨桌面卡片、图标、空状态、错误提示和 Web/iOS 管理体验 |
| Lua/LVGL App 能力 | 已接入 Lua 5.5 完整语言 VM；LVGL/硬件函数仍是受控 binding，未开放 `os/io/debug/package` 和动态 C 模块 | 按真实 App 增加高价值 binding，并补对象配额、长稳和异常恢复 |
| GUI 管理工具 | Mac 脚本、BLE 脚本、iOS Demo 已能验证，但还偏工程工具 | 收敛成手机/桌面 App 的安装、管理、日志、故障提示和示例市场入口 |
| App 生态 | 已有传感器、电源、显示、GPIO、触摸、语音、信息流等 stage app，但还不是 C 端应用集 | 把 stage app 打磨成天气、语音助手、传感器仪表、工具面板等真实可用 App |
| 高阶能力 | 已有受控 WAV 播放；仍没有 NES/native module、gamepad、复杂网络和相机 | 先验证 Lua/audio 长稳，再逐项评估其余高风险能力 |
| 商业化闭环 | 还缺量产测试、固件/资源 OTA 策略、崩溃日志、权限模型、签名包、用户文档和售后恢复流程 | 先建立发布 gate 和恢复链路，再定义面向 C 端的安装/升级/回滚体验 |

黄山派目前做得更好的地方也很明确：第一，它优先适配了黄山派真实硬件，
对 CO5300、FT6146、AW32001、VBAT、KEY、板载 RGB、LSM6DSL 和缺席传感器
都有实际探测与降级逻辑，不会把 I2C 未 ACK 直接变成崩溃。第二，它走
BLE/iPhone 优先路线，不要求板子自己联网或让用户输入 Wi-Fi 密码；PAN / HTTP App OTA 也不会在默认产品固件中导出，后续更适合
手机端一键安装和管理。第三，串口/BLE/iOS 共用长 JSON fallback、包校验、
staging 提交、abort 和冷启动恢复机制，工程可靠性比单纯“能传文件”更靠近
产品底座。第四，完整 Lua 语言能力和受控 host binding 分层：App 能写正常 Lua，
但不能绕过 manifest 权限、文件沙箱和硬件白名单，更适合 AI 生成 App。

## 推荐本地目录

建议把 SDK、立创例程和本项目放在同一个目录下：

```text
/Users/hushaohong/vibe-coding/huangshan-pi-workspace/
  sifli-sdk/                  官方 SDK
  lckfb-hspi-ulp_example/     立创黄山派参考例程
  huangshan-pi-sf32-dev/      本开发仓库
```

本仓库的脚本默认会从兄弟目录 `../sifli-sdk` 查找 SDK。如果你把 SDK 放在
其他位置，可以通过 `SIFLI_SDK_PATH` 覆盖。

## 仓库结构

```text
project/                 SCons 工程文件
src/gui_apps/            当前启动器里的应用模块
src/gui_apps/VibeBoard_Runtime/ 一次烧录后的串口 App 更新 Runtime
scripts/runtime_apps/codex_pet/ Codex Companion 的 Lua 入口、提示音和本地生成资源
src/gui_apps/Huangshan_UI_Lab/ AI/LVGL 组件化 UI 示例
src/huangshan_ui/         built-in 与 Runtime 共用的受限 LVGL 主题/组件库
src/gui_apps/Codex_Test/ 第一个已验证的自定义应用
src/resource/images/     SiFli 资源工具转换后的图片资源
src/resource/strings/    多语言字符串资源
scripts/                 本地构建、烧录、串口监视脚本
docs/                    开发板、上游来源和 bring-up 记录
```

## 资料文档

仓库内已整理 SiFli 和黄山派相关资料：

- [SiFli 资料索引](docs/sifli-resources.md)
- [SiFli SDK 本地地图](docs/sifli-sdk-map.md)
- [黄山派学习路线](docs/sifli-learning-path.md)
- [黄山派 AI 协作开发实测复盘](docs/huangshan-pi-ai-development-worklog.md)
- [板子与应用分离：SiFli SDK 的开发模型](docs/board-app-separation.md)
- [黄山派 AI UI Lab](docs/ai-ui/README.md)
- [对标 vibeboard-runtime-gpl 的 Runtime 进度](docs/vibeboard-runtime-gpl-parity.md)
- [黄山派 Runtime 边界](docs/runtime-boundary.md)
- [黄山派 Runtime App 包格式](docs/runtime-package-format.md)
- [黄山派 Runtime 能力表](docs/runtime-capabilities.md)
- [黄山派 Runtime 高阶能力评估](docs/runtime-high-risk-capabilities-evaluation.md)
- [黄山派 Runtime App Plan Writer](docs/runtime-app-plan-writer.md)
- [Runtime App 开发经验记录](docs/runtime-app-development-notes.md)
- [Codex Pet Bridge、任务状态与审批协议](docs/codex-pet-bridge.md)
- [Codex Pet 音频、TTS 与唤醒词评估](docs/codex-pet-audio-evaluation.md)
- [黄山派 LVGL AI 生成约束](docs/ai-ui/lvgl-system-contract.md)
- [上游仓库记录](docs/upstream.md)
- [开发板 bring-up 记录](docs/board-bringup.md)

## 构建

macOS / Linux：

```bash
./scripts/build.sh
```

Windows PowerShell：

```powershell
.\scripts\build.ps1
```

两个脚本默认都使用：

```text
BOARD=sf32lb52-lchspi-ulp
JOBS=8
SDK=../sifli-sdk
```

如果 SDK 不在兄弟目录，可以用 `SIFLI_SDK_PATH` 覆盖。Windows 还可以用
`SIFLI_SDK_TOOLS_PATH` 指定 SiFli 工具缓存目录；不指定时默认使用本仓库
下的 `.sifli-tools/`，该目录不会提交到 Git。

等价的手动命令：

```bash
cd project
source ../../sifli-sdk/export.sh
scons --board=sf32lb52-lchspi-ulp -j8
```

```powershell
. ..\..\sifli-sdk\export.ps1
cd project
scons --board=sf32lb52-lchspi-ulp -j8
```

## Windows 首次环境

Windows 需要先按 SiFli SDK 官方流程安装工具链：

```powershell
cd ..\sifli-sdk
$env:SIFLI_SDK_TOOLS_PATH="..\huangshan-pi-sf32-dev\.sifli-tools"
.\install.ps1
```

如果国内网络访问 GitHub 慢，可以在安装前加：

```powershell
$env:SIFLI_SDK_GITHUB_ASSETS="downloads.sifli.com/github_assets"
```

## 烧录

macOS / Linux：

```bash
./scripts/flash.sh /dev/cu.usbserial-13220
```

烧录脚本会先校验构建产物和串口类型，然后调用 SiFli `sftool`。默认只把
`/dev/cu.usbserial*` / `/dev/ttyUSB*` 这类 CH340/UART bridge 端口当作
黄山派推荐下载口；`/dev/cu.usbmodem*` 通常是 USB CDC 设备，脚本会拒绝
直接使用，除非显式传给底层 Python 工具 `--allow-usbmodem`。

查看本机候选串口：

```bash
./scripts/flash.sh --list-ports
```

做多轮烧录成功率测试：

```bash
./scripts/flash_reliability.sh /dev/cu.usbserial-13220 --runs 5 --confirm-boot
```

Windows PowerShell：

```powershell
.\scripts\flash.ps1 COM7
```

Windows 串口也可以只传数字，例如 `.\scripts\flash.ps1 7`。烧录脚本使用
UART 下载，需要先完成构建并生成 `bootloader.bin`、`main.bin` 和
`ftab.bin`。

## Runtime App 更新


### 记录 Runtime App 开发经验

如果某个 App 开发成功后，需要把开发过程中遇到的问题、误判、修复方式和后续规则沉淀下来，可以直接对 Codex 说：

```text
把这次 App 开发过程中遇到的问题和解决经验，记录到 Runtime App 开发经验文档里。
```

也可以说得更具体：

```text
把刚刚开发这个 App 时遇到的触摸/显示/安装问题，总结进 docs/runtime-app-development-notes.md，按现有格式写：现象、原因、修复方式、后续规则和验证方式。
```

或者在一次功能完成后说：

```text
复盘这次 App 开发，把值得后续复用的坑和经验沉淀到开发经验文档。
```

听到这些指令时，Codex 应默认更新 [`docs/runtime-app-development-notes.md`](docs/runtime-app-development-notes.md)，并尽量按该文档里的固定格式记录：日期 / App、现象、容易误判的方向、真正原因、修复方式、后续规则和验证方式。

`VibeBoard_Runtime` 是面向“一次烧录固件，之后不重刷固件更新 App”的
第一版黄山派 Runtime。固件启动后，板端首页会直接扫描 `/sdcard/apps`，以卡片形式显示已安装且可用的 Runtime App；点击卡片会写入 `.active` 并进入 Runtime 启动对应 App，不需要再先进入一个单独的 App Manager 页面。

Runtime 固件负责屏幕、触摸、文件系统、串口命令和 App 包加载。App 更新
只改逻辑路径：

```text
/sdcard/apps/<appId>/
  manifest.json
  main.lua
  assets/...
/sdcard/apps/.active
```

当前黄山派板型不要求自己联网。第一版通过 Mac Mini 串口桥安装 App。串口桥也
可以先读取 Runtime 能力握手和传感器 JSON，供 AI/电脑端决定下一步调用。安装前可先在主机侧校验
manifest schema、App id、入口文件、组件类型/能力白名单、包内路径白名单和 `main.lua` 脚本子集；`--self-test` 会覆盖合法包和常见坏包负例，避免校验器本身退化：

```bash
./scripts/runtime_package.py --self-test
./scripts/runtime_package.py --all
./scripts/runtime_package.py --package-dir scripts/runtime_apps/display_stage
./scripts/runtime_package.py --package-dir scripts/runtime_apps/voice_stage
```

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --capabilities-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --sensors-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --power-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --display-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --display-brightness 70
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --gpio-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --touch-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --voice-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --app-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --apps-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --launch-app flow_stage
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --stop-app
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --delete-app old_app
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 \
  --package-dir scripts/runtime_apps/clock_test \
  --app-id clock_test
```

安装会先写入隐藏 staging 目录，`install_end` 校验完整包和 manifest 最低 schema
后再提交到 `/sdcard/apps/<appId>`；中途断包或 `install_end` 失败时，可执行
`install_abort` 清理 staging，串口/BLE 安装脚本也会在命令失败后自动尝试 abort。
当前标准查询/管理命令、普通安装、`--stop-before-end` 故障注入、`--abort-only`、flow、语音桥和跨重连 flow persist 都已经共用
`scripts/runtime_transport.py` 里的 serial/BLE adapter。允许保留脚本内直连的只有底层例外：
BLE scan/connect-hold、冷启动恢复里的 RTS 复位、烧录脚本和串口 monitor。
这些路径服务于连接发现或板级恢复，不属于普通 App 管理 API。
Runtime 启动时还会清理遗留 `.__install_*` 目录，并在发现孤立 `.__backup_*`
目录时尝试恢复旧 App。

验证 10 次免烧录安装/切换：

```bash
./scripts/runtime_reliability.sh /dev/cu.usbserial-13220 --runs 10
./scripts/runtime_reliability.sh /dev/cu.usbserial-13220 --runs 6 --verify-abort
./scripts/runtime_reliability.sh /dev/cu.usbserial-13220 \
  --runs 3 \
  --verify-capabilities \
  --verify-sensors \
  --verify-power \
  --verify-display \
  --verify-touch \
  --verify-gpio \
  --verify-rgb \
  --verify-flow \
  --verify-voice-status \
  --verify-audio-status \
  --apps audio_stage display_stage gpio_keys_stage power_stage touch_stage voice_stage
./scripts/runtime_full_reliability.sh \
  --package-only \
  --apps audio_stage display_stage gpio_keys_stage power_stage touch_stage voice_stage
./scripts/runtime_full_reliability.sh /dev/cu.usbserial-13220 \
  --runs 3 \
  --core \
  --apps audio_stage display_stage gpio_keys_stage power_stage touch_stage voice_stage
./scripts/runtime_reliability.sh /dev/cu.usbserial-13220 \
  --runs 1 \
  --verify-voice \
  --voice-duration-ms 600 \
  --apps touch_stage
./scripts/runtime_reliability.sh /dev/cu.usbserial-13220 \
  --runs 3 \
  --verify-abort \
  --verify-cold-recovery \
  --boot-seconds 12
./scripts/runtime_cold_recovery.sh /dev/cu.usbserial-13220 \
  --active-app clock_test \
  --stage-app status_test \
  --boot-seconds 12
```

其中 `runtime_reliability.sh --verify-capabilities` 会在每轮安装后补读一遍 Runtime
能力 JSON，确认不会再出现截断，并校验 `rt/ins/app/hw` 这些握手字段；
`--verify-sensors` 会补读 `vb_runtime_sensors` JSON，确认当前实测硬件组合下至少还有
一组板载传感器在线，并且 `count/ready` 与各子项 `ok` 状态一致；
`--verify-power` 会补读 `vb_runtime_power` JSON，确认 VBAT / charger 只读快照接口
稳定返回；`--verify-display` 会补读 `vb_runtime_display` JSON、设置一次亮度、复读并恢复原亮度，确认屏幕快照和亮度 API 稳定；`--verify-touch` 会补读 `vb_runtime_touch` JSON，确认触摸 API 在 Runtime
启动后无需先手动触屏也能稳定返回 idle 快照；`--verify-gpio` 会继续补读
`vb_runtime_gpio` JSON，确认 KEY1/KEY2 白名单输入 API 能稳定返回；`--verify-rgb`
会在同一轮验证里完成 RGB 设色、读回和恢复，确认 `vb_runtime_rgb` 的会话态与
返回 JSON 一致；`--verify-flow` 会在同一轮里依次执行 `flow_clear`、`flow_send`、`flow_status`、`flow_clear`，确认信息流历史、计数和最新 seq/channel 都能闭环；`--verify-voice-status` 只读 `vb_runtime_voice` JSON；`--verify-audio-status` 只读播放状态并验证 capability；`--verify-voice` 会跑一轮串口语音桥闭环，抓取板载麦克风短录音、
保存 WAV、回写 `pc.voice` 信息流，并检查最新 JSONL 证据是否包含有效音频和成功 ack。
当前串口语音桥默认把 `voice_read` 单次 payload 控制在 30 bytes，以避开 MSH 单行长度限制；串口和 BLE 语音桥的采集、分片读取和 `pc.voice` 回写都通过 `scripts/runtime_transport.py` 的统一 adapter 执行。
`runtime_full_reliability.sh` 默认会先校验选中的 Runtime App 包，再碰硬件；加 `--package-only` 可以只跑离线包校验，不需要串口参数。`runtime_full_reliability.sh --core` 会把串口和 BLE 两条链路按顺序跑完，适合做发布前 gate；串口和 BLE 都覆盖安装、核心硬件 JSON、只读 `voice-status` 和 `audio-status`。不要同时并行跑串口和 BLE 安装回归，因为同一块板子的安装 session 是共享状态，并行访问会制造 `session=--` 或旧 status 误判这类伪故障。
`runtime_reliability.sh --verify-cold-recovery` 会在常规安装/abort
检查结束后，继续运行单串口会话的冷启动恢复验证；`runtime_cold_recovery.sh`
则保留为直接调用入口。冷恢复脚本会在同一个串口会话里完成 active App 安装、
staging、强制复位和 boot log 抓取，避免宿主机重复开关串口时提前触发板子复位，
便于稳定验证 `.__install_*` 冷启动清理和 active App 保持。

板端 Runtime 暴露的 MSH 命令：

```text
vb_runtime_status
vb_runtime_install_begin <appId>
vb_runtime_install_file <appId> <path> <offset> <hexChunk>
vb_runtime_install_abort <appId>
vb_runtime_install_end <appId>
vb_runtime_select <appId>
vb_runtime_reload
vb_runtime_app
vb_runtime_apps
vb_runtime_launch <appId>
vb_runtime_stop
vb_runtime_delete <appId>
vb_runtime_quiet [0|1]
vb_runtime_capabilities
vb_runtime_sensors
vb_runtime_power
vb_runtime_display [brightness]
vb_runtime_gpio
vb_runtime_touch
vb_runtime_rgb [off|red|green|blue|yellow|cyan|magenta|white|RRGGBB]
vb_runtime_sensor_probe
vb_runtime_ble_status
vb_runtime_flow_status
vb_runtime_flow_send <channel> <seq> <hexUtf8Payload>
vb_runtime_flow_clear
vb_runtime_voice
vb_runtime_voice_status
vb_runtime_voice_start <durationMs>
vb_runtime_voice_stop
vb_runtime_voice_read <offset> <maxBytes>
vb_runtime_voice_clear
```

串口日志里应能看到 `[vb_runtime] install complete: <appId>`、
`[vb_runtime] active=<appId>` 和 `transport=serial-msh`。当前版本支持
manifest 驱动的 LVGL 展示、App 包落盘，以及 Lua 5.5 完整语言 VM；状态显示
`lua=lua-5.5-full`。函数、闭包、表、循环、条件和 App 本地模块都可用，同时限制
脚本大小、内存、指令数、文件范围和 host API。`vb_runtime_app` 返回当前 Runtime 管理状态 JSON，包含 active app、运行/失败状态、启动/停止计数和最近错误；`vb_runtime_apps` 返回已安装 App 列表，并标记 manifest/app.info/main.lua 是否存在、是否兼容和是否 active。`vb_runtime_launch <appId>` 会先校验 App 存在且兼容，再写入 active 并异步 reload；`vb_runtime_stop` 会停止当前 App、清空 `.active` 为 `welcome` 并回到板端首页；`vb_runtime_delete <appId>` 只允许删除已停止的 App，避免用户误删正在运行的包。板端 UI 不再保留单独 App Manager 页面；日常启动从首页卡片完成，安装/删除/刷新等管理动作由本地 Web 或 iOS/桌面工具完成。主机侧包校验接受完整 Lua 语法，但拒绝 `os/io/debug/package`、任意文件访问、已禁用能力和未知 host helper。当前 manifest 最低约束是
`kind=huangshan-runtime-app-manifest`、`id=<appId>`、`entry=main.lua`，以及
`schemaVersion`/`version` 为 `1`；manifest 组件当前会校验 `status/clock/action/label` 类型，以及 sensor/power/display/voice/touch/gpio/clock/status/reload 等已知能力白名单。若 manifest 声明 `runtimeProfile` / `targetProfile` / `target`，必须指向 `huangshan` / `huangshan-pi` / `vibeboard-huangshan` / `sf32*`；若声明顶层 `capabilities` / `requires` / `permissions`，打包器会按黄山派 profile 校验，并拒绝 `wifi`、`http`、`network`、`ntp`、`board_ip`、`native`、`camera`、`gamepad`、`i2s` 等 ESP32 或板载联网时代的能力。`weather.current` 这类能力表示手机/桌面 bridge 已经注入的数据，不表示板子自己联网；旧的 `app.info` 兼容入口仍然保留。当前 host binding 支持 `print(...)`、基础
LVGL label/button/image、文本/颜色/位置/尺寸、文件读取证据、简单 tick
label、`vibe_rgb("red|green|blue|off|RRGGBB")` 控制板载 RGB LED，
`vibe_display_brightness("0..100")` 设置屏幕亮度，
以及 `vibe_sensor_label(label, "light|acce|gyro|mag|step")` 读取板载
传感器首帧快照。Manifest 组件也可以声明 `sensor.light`、`sensor.acce`、
`sensor.gyro`、`sensor.mag`、`sensor.step`，Runtime 会定时刷新显示值；
声明 `power.battery` / `battery` 时会显示只读 VBAT 电压，声明
`power.charger` / `power.charger.status` / `power.charger.state` / `power.charger.det` / `power.charger.en` / `power.charger.fault`
时会显示 AW32001 只读充电状态字段，`power_stage` 是对应示例包；声明
`display.brightness`、`display.size`、`display.resolution`、`display.state` 或 `display.bpp` 时会显示屏幕亮度/面板信息，`display_stage` 是对应示例包；声明
`voice.ready`、`voice.recording`、`voice.state`、`voice.seq`、`voice.bytes`、`voice.duration`、`voice.dropped`、`voice.error`、`voice.rate`、`voice.built` 或 `voice.available` 时会显示语音桥只读状态；声明 `voice.start` / `voice.record` 或 `voice.clear` action 时会通过 Runtime 受控触发短录音或清空录音，`voice_stage` 是对应示例包；声明 `flow.latest`、`flow.payload`、`flow.channel`、`flow.seq`、`flow.bytes`、`flow.total`、`flow.retained` 或 `flow.capacity` 时会只读显示最近一条信息流，`flow_stage` 是对应示例包；声明
`gpio.key1`、`gpio.key1.level`、`gpio.key2`、`gpio.key2.level` 时会显示
KEY 白名单 GPIO 状态；声明 `touch.count`、`touch.last`、`touch.event`、`touch.gesture`、`touch.delta`、`touch.duration`、`touch.active`
时会显示 Runtime 记录到的触摸状态。Lua host API 还包括
`vibe_power_label(label, "battery|charger|charger.status|charger.state|charger.det|charger.en|charger.fault")`、
`vibe_display_label(label, "brightness|size|resolution|state|bpp")`、
`vibe_voice_label(label, "ready|recording|state|seq|bytes|duration|dropped|error|rate|built|available")`、
`vibe_voice_start("0..3000")` / `vibe_voice_stop()` / `vibe_voice_clear()` 受控请求短录音、保留当前录音和清空录音，
`vibe_flow_label(label, "latest|payload|channel|seq|bytes|total|retained|capacity")` 只读展示最新信息流，
`vibe_gpio_label(label, "key1|key1.level|key2|key2.level")` 和
`vibe_touch_label(label, "count|last|event|gesture|delta|duration|active")`，以及
`vibe_audio_play("assets/file.wav")`、`vibe_audio_stop()`、
`vibe_audio_volume(0..15)` 和 `vibe_audio_label(...)`。复杂 Lua 语法不再要求
更新固件；网络、新 LVGL binding、底层 I2S 或新的硬件 helper 仍需要 Runtime 更新。
`vb_runtime_quiet` 默认开启，用来压低自动运行 App 的周期性日志，避免影响
串口或 BLE 安装调试；需要观察游戏 tick 时可执行 `vb_runtime_quiet 0`。

`vb_runtime_capabilities` 返回一行机器可读 JSON，作为手机端和 AI 端的握手入口。
它声明当前固件支持的 Runtime API 版本、串口/BLE 安装、manifest/Lua 5.5 VM、
资源包、显示/触摸、传感器、语音桥、信息流、只读电池电压、AW32001
只读充电状态、屏幕亮度/面板快照、KEY 白名单 GPIO、板载 RGB LED 和音频播放。当前 `battery` / `charger` 已同时开放 JSON、manifest status 和 `vibe_power_label(...)` App 侧展示路径；`display` 已开放 JSON、manifest status、`vibe_display_label(...)` 和 `vibe_display_brightness(...)` App 侧路径；`voice` 已开放只读 JSON、manifest status/action、`vibe_voice_label(...)`、`vibe_voice_start(...)` 和 `vibe_voice_clear()` App 侧路径；`audio` 已开放 PCM WAV 播放、停止、音量和状态；`flow` 已开放 host 写入、Runtime 历史、manifest status 和 `vibe_flow_label(...)` App 侧只读展示路径；`battery`、`charger`、`display`、`gpio`、`rgb` 和 `audio`
都会标为 `1`；其中 `gpio` 当前只表示 KEY1 / KEY2 这组只读白名单输入可用，
并不代表任意通用 GPIO、LED1 或其他输出 pin 已向 App 开放。传感器每颗是否在线继续由
`vb_runtime_sensors` 返回。

`vb_runtime_sensors` 会探测并初始化 I2C3 上的板载 LTR303 光照、MMC56X3
磁力、LSM6DSL 加速度/陀螺仪/计步器，并返回一行 JSON，供 AI、手机 App 或
Mac 串口桥直接解析。Runtime 会先探测 I2C 地址，只有设备应答后才初始化对应
驱动，因此不会因为某颗传感器缺席而触发官方例程里的断言。

当前实测板子上 LSM6DSL 可用，LTR303 和 MMC56X3 未应答，因此返回示例是：

```json
{"api":"vibeboard-huangshan-sensors/v1","available":1,"ready":1,"count":3,"light":{"ok":0,"lux":0},"mag":{"ok":0,"x":0,"y":0,"z":0},"acce":{"ok":1,"x":-9,"y":48,"z":-986},"gyro":{"ok":1,"x":-70,"y":-18760,"z":-13720},"step":{"ok":1,"count":0}}
```

`ready=1` 表示至少一个板载传感器可读；`count` 表示本次成功读到的逻辑传感器数量；每个子对象的 `ok` 表示该传感器本次
是否在线并成功读取。可执行 `vb_runtime_sensor_probe` 查看 I2C3 上
LTR303/MMC56X3/LSM6DSL 的应答情况。当前实测探测结果为 LSM6DSL `0x6a` OK，
LTR303 `0x29` 和 MMC56X3 `0x30` 未 ACK。

`vb_runtime_power` 返回只读电源 JSON。当前稳定承诺范围是 VBAT 电压，以及
AW32001 `SYS_STATUS/FAULT/POWERON_CONF` 寄存器的只读快照；Runtime 不写充电
寄存器，也不调用带副作用的充电控制接口。App 侧可通过 manifest `power.charger*`
或 `vibe_power_label(...)` 展示这些只读字段。示例：

```json
{"api":"vibeboard-huangshan-power/v1","available":1,"ready":1,"battery":{"ok":1,"mv":4350,"raw":43504,"dev":"bat1","ch":7},"charger":{"ok":1,"state":0,"status":"no_charging","det":0,"en":1,"sys":0,"fault":0}}
```


`vb_runtime_display [brightness]` 返回或设置当前 Runtime 向 App 开放的屏幕 JSON。首版只承诺 CO5300 面板快照和 0-100 亮度读写：`width/height/bpp/format/align` 来自 LCD graphic info，`state/state_name` 来自 LCD 状态，`brightness` 来自 LCD 背光控制。为了不影响调试和安装，当前不开放熄屏、开屏或显示电源控制。示例：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --display-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --display-brightness 70
```

```json
{"api":"vibeboard-huangshan-display/v1","available":1,"ready":1,"ok":1,"dev":"lcd","width":390,"height":450,"bpp":16,"format":1,"align":4,"brightness":70,"state":4,"state_name":"on"}
```

配套示例包 `scripts/runtime_apps/display_stage/` 会设置一次亮度并展示 `brightness/size/state`。

`vb_runtime_gpio` 返回当前 Runtime 向 App 开放的 GPIO 白名单 JSON。第一版只开放
`KEY1` 和 `KEY2` 两个只读输入，字段同时给出 `level` 和已经按板级极性换算后的
`pressed`。示例：

```json
{"api":"vibeboard-huangshan-gpio/v1","available":1,"ready":1,"count":2,"inputs_only":1,"key1":{"ok":1,"pin":34,"active_high":1,"level":0,"pressed":0},"key2":{"ok":1,"pin":43,"active_high":1,"level":1,"pressed":1}}
```

当前这版故意不开放任意 pin 读写，也不把 `LED1/GPIO26`、`PA20` 之类输出脚直接给
App 使用；先把安全白名单稳定下来，再继续评估背光、LED 和其他板级控制接口。

配套示例包 `scripts/runtime_apps/gpio_keys_stage/` 会在屏幕上展示 `key1/key2` 的
`pressed` 和 `level`。

`vb_runtime_touch` 返回最近一次 Runtime 触摸 JSON。当前这版不是去直接抢底层
`touch` 设备，而是由 Runtime 自己在 LVGL 事件里记录最近一次触摸点位、事件类型、
手势方向、按住时长和按下到当前点的位移，因此更适合给 manifest / Lua / 手机端读取。示例：

```json
{"api":"vibeboard-huangshan-touch/v1","available":1,"ready":1,"active":0,"count":3,"x":122,"y":308,"event":"released","gesture":"left","dx":-84,"dy":6,"duration_ms":412,"tick":4567}
```

`count` 表示 Runtime 观测到的按下开始次数；`x`,`y` 表示最近一次触摸点；`event` 当前会返回
`pressed`、`pressing`、`released`、`clicked` 或 `gesture`；`gesture` 会返回 `left`、`right`、`up`、`down`
或 `none`；`dx`,`dy` 表示本次按下起点到当前/释放点的位移；`duration_ms` 表示最近一次按住持续时间。

配套示例包 `scripts/runtime_apps/touch_stage/` 已更新为展示 `gesture`、`delta`、`duration` 和
`active` 这些更高层的 Runtime 触摸语义。

`vb_runtime_rgb` 返回或设置板载 RGB LED 状态。颜色参数支持 `off`、`red`、
`green`、`blue`、`yellow`、`cyan`、`magenta`、`white` 或 6 位十六进制 RGB。RGB
状态是 Runtime 会话态，板子重启后默认回到 `off`；需要自动恢复颜色时，把
`vibe_rgb("3366ff")` 放进当前 active app 的 `main.lua`。
示例：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --display-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --gpio-only
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --rgb-color 3366ff
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --rgb-only
```

```json
{"api":"vibeboard-huangshan-rgb/v1","available":1,"ready":1,"ok":1,"dev":"rgbled","count":1,"color":"3366ff","name":"custom"}
```

`vb_runtime_voice` 返回语音桥只读状态 JSON；BLE 侧命令名是 `voice`，主机脚本是
`--voice-only`。这个接口只描述当前录音桥状态，不会启动麦克风采集。App 侧如需触发短录音，可以声明 manifest action `voice.start` / `voice.record`，其中 `value` 是毫秒数并会被 Runtime 限制到 0-3000 ms；录音缓冲按本次时长分配，`voice.clear` 会清空状态并释放 PCM。Lua host API 也提供 `vibe_voice_start("1000")` 和 `vibe_voice_clear()`，但 App 仍不能直接读取 PCM，音频数据读取继续由串口/BLE 桥的 `voice_read` 完成。示例：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --voice-only
```

```json
{"api":"vibeboard-huangshan-voice-bridge/v1","available":1,"built":1,"ready":0,"recording":0,"seq":0,"requested_ms":0,"bytes":0,"rate":16000,"bits":16,"channels":1,"dropped":0,"err":0}
```

配套示例包 `scripts/runtime_apps/voice_stage/` 会在屏幕上展示 `ready/recording/bytes/seq`，并提供 `Record 1s` / `Clear` 两个受控 action 按钮。

安装示例传感器面板：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 \
  --package-dir scripts/runtime_apps/sensor_dash \
  --app-id sensor_dash
```

安装示例显示面板：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 \
  --package-dir scripts/runtime_apps/display_stage \
  --app-id display_stage

./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --display-only
```

安装示例 GPIO 面板：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 \
  --package-dir scripts/runtime_apps/gpio_keys_stage \
  --app-id gpio_keys_stage

./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --gpio-only
```

安装示例触摸面板：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 \
  --package-dir scripts/runtime_apps/touch_stage \
  --app-id touch_stage

# 遇到半包或想手动清 staging 时
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 \
  --abort-only --app-id touch_stage
```

安装示例语音状态面板：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 \
  --package-dir scripts/runtime_apps/voice_stage \
  --app-id voice_stage

./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --voice-only
```

安装示例信息流面板：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 \
  --package-dir scripts/runtime_apps/flow_stage \
  --app-id flow_stage

./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 \
  --flow-send-text "hello from host" --flow-channel pc.demo
./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --flow-status-only
```

Runtime 也会默认广播 BLE 设备名 `VibeBoard`，用于后续手机/VibeCoding App
通过 BLE GATT 分块安装 App 包。板端状态命令：

```text
vb_runtime_ble_status
```

当前 BLE 安装服务已验证会返回 `init=1 power=1 service=1 adv=1`；如果 Mac
短时间内连过但又扫不到设备，可先通过串口执行 `vb_runtime_ble_restart` 强制
stop/start 广播，再重新扫描。iPhone 不一定会在系统“设置 > 蓝牙”里显示这种
自定义 BLE GATT 设备；验证时请用 LightBlue、nRF Connect 或后续 VibeCoding
手机端扫描 `VibeBoard`。

Mac 也可以作为 BLE 中心端验证这条手机同款链路：

```bash
python3 -m venv .venv-ble
.venv-ble/bin/python -m pip install bleak
./scripts/runtime_install_ble.sh --scan-only
./scripts/runtime_install_ble.sh --status-only
./scripts/runtime_install_ble.sh --capabilities-only
./scripts/runtime_install_ble.sh --sensors-only
./scripts/runtime_install_ble.sh --power-only
./scripts/runtime_install_ble.sh --display-only
./scripts/runtime_install_ble.sh --display-brightness 70
./scripts/runtime_install_ble.sh --gpio-only
./scripts/runtime_install_ble.sh --touch-only
./scripts/runtime_install_ble.sh --voice-only
./scripts/runtime_install_ble.sh --app-only
./scripts/runtime_install_ble.sh --apps-only
./scripts/runtime_install_ble.sh --launch-app flow_stage
./scripts/runtime_install_ble.sh --stop-app
./scripts/runtime_install_ble.sh --delete-app old_app
./scripts/runtime_install_ble.sh \
  --flow-persist-text "hello over BLE" \
  --flow-channel phone.persist
./scripts/runtime_install_ble.sh \
  --package-dir scripts/runtime_apps/clock_test \
  --app-id clock_test
./scripts/runtime_reliability_ble.sh \
  --runs 1 \
  --verify-capabilities \
  --verify-sensors \
  --verify-power \
  --verify-display \
  --verify-touch \
  --verify-gpio \
  --verify-rgb \
  --verify-flow \
  --verify-voice-status \
  --verify-audio-status \
  --apps audio_stage display_stage gpio_keys_stage power_stage touch_stage voice_stage
./scripts/runtime_reliability_ble.sh \
  --runs 1 \
  --verify-voice \
  --voice-duration-ms 600 \
  --apps touch_stage
```

`runtime_reliability_ble.sh` 会走和手机端一致的 BLE GATT 安装链路，自动验证
App 安装、`capabilities` 长 JSON 读取，以及核心硬件 JSON、只读 `voice-status`、
`audio-status` 和真实 `voice` 录音链路
这些 Runtime API 是否还能完整返回；其中 `flow` 会通过 `--flow-persist-text` 覆盖 BLE 断开重连后仍能从 `/sdcard/apps/.flow` 恢复最近一条消息。

当前语音桥已经在两条链路都做过真机闭环：串口走
`runtime_reliability.sh --verify-voice`，BLE 走
`runtime_reliability_ble.sh --verify-voice`。两边都会把录音保存成 WAV，再把
文本回复通过 `flow_send pc.voice ...` 回写到 Runtime 信息流。App 可通过 manifest `flow.latest` / `flow.payload` / `flow.channel` / `flow.seq` / `flow.bytes` / `flow.total` 等字段，或 Lua `vibe_flow_label(...)`，只读展示最近一条 host/手机端信息。Runtime 会把最近一条信息流保存到 `/sdcard/apps/.flow`，重启或串口重连复位后会在加载 active app 前恢复；`flow_clear` 会同步清掉这份状态。Lua App 如果执行
`lv_obj_clean(root)`，Runtime 现在也会同步清空 `flow_label` 缓存，避免语音回复
回写后访问悬空指针导致 hard fault。

电脑端也可以走同一条 BLE 链路拉取黄山派麦克风短录音，保存成 WAV，在本机
交给大模型/语音模型处理后，再把文本结果回写到黄山派 Runtime 屏幕的信息流：

```bash
./scripts/voice_terminal.sh --preflight
./scripts/voice_terminal_selftest.sh
export OPENAI_API_KEY=...
./scripts/voice_terminal.sh --interactive
./scripts/voice_terminal.sh --once --duration-ms 1800 --print-transcript --yes-record
./scripts/voice_terminal_verify.sh

export ZHIPU_API_KEY=...
./scripts/voice_terminal.sh --provider zhipu --preflight
./scripts/voice_terminal.sh --provider zhipu --once --duration-ms 1800 --print-transcript --yes-record
./scripts/voice_terminal_verify.sh

# Or keep the key in local-only .voice_terminal.env:
# ZHIPU_API_KEY=...

./scripts/voice_terminal.sh --status-only
./scripts/voice_terminal.sh --self-test
./scripts/voice_bridge_serial.sh /dev/cu.usbserial-13220 --status-only --no-echo
./scripts/voice_bridge_serial.sh /dev/cu.usbserial-13220 \
  --send-reply-only \
  --reply-text "串口信息流回写验证 OK" \
  --no-echo
./scripts/voice_bridge_ble.sh --status-only
./scripts/voice_bridge_ble.sh \
  --send-reply-only \
  --reply-text "电脑端信息流回写验证 OK"
./scripts/voice_bridge_ble.sh --duration-ms 1500
./scripts/voice_bridge_ble.sh \
  --duration-ms 2500 \
  --reply-command './scripts/voice_llm_openai.sh --wav {wav}'
./scripts/voice_bridge_ble.sh \
  --interactive \
  --duration-ms 1800 \
  --reply-command './scripts/voice_llm_openai.sh --wav {wav}'
./scripts/voice_bridge_ble.sh \
  --duration-ms 1800 \
  --reply-command './scripts/voice_llm_zhipu.sh --wav {wav} --metadata-json {wav}.zhipu.json'
```

串口和 BLE 的 `--status-only` / `--send-reply-only` 都不会启动麦克风采集，适合先验证电脑和板子的
信息流回写；带 `--duration-ms` 的录音路径会通过板载麦克风采集短音频。
`voice_terminal.sh --interactive` 会保持一个终端会话，按回车录一轮、处理一轮、
回写一轮。`voice_terminal.sh` 默认使用 OpenAI；加 `--provider zhipu` 后使用智谱，
读取 `ZHIPU_API_KEY`、`ZHIPUAI_API_KEY` 或 `BIGMODEL_API_KEY`。两种 provider 都会
先转写 WAV，再生成适合小屏幕显示的短回复。`--preflight` 会在不录音的情况下检查
BLE 语音桥和本地模型 helper；
`--self-test` 不联网、不录音，只检查本地模型 helper 的请求打包和响应解析逻辑。
`voice_terminal_selftest.sh` 不需要蓝牙硬件，用来回归终端入口的参数和录音确认逻辑。
录音模式会要求输入 `record` 确认；明确授权后的自动化运行可以加 `--yes-record`。
成功录音/回写后会追加一条 JSONL 证据到 `captures/voice_terminal.jsonl`，包含 WAV
路径、模型转写/回复元数据、回复文本和板端 ack；`voice_terminal.sh` 和直接运行的
`voice_bridge_ble.sh` 默认都写这份日志，必要时可用 `--log-jsonl` 覆盖；
`voice_terminal_verify.sh` 会检查最新证据是否包含有效 WAV、非空回复和成功的板端
`flow_send` ack。`voice_bridge_serial.py` 和 `voice_bridge_ble.py` 只保留 WAV、JSONL、交互和模型命令这些终端层逻辑；底层 `voice_*`、`flow_send` 和 ACK/分片解析统一复用 `scripts/runtime_transport.py` 的 serial/BLE adapter。

通用短录音接口仍使用 `voice_start <ms>`、`voice_stop`、`voice_status`、
`voice_read <offset> <maxBytes>`、`voice_clear`，并以 16 kHz、16-bit、mono PCM
分块读取。Pager 按住说话使用独立的 BLE Notify 流：板端实时编码为 16 kHz
G.711 mu-law，电脑端边录边收并恢复为 16-bit PCM，因此不受 3 秒短录音缓存限制。

Pager 的 `Compose` 已替换为按住说话。松手后，电脑端常驻桥接程序只调用智谱
`glm-asr-2512` 做转写，再通过 `pager.compose` 回传文字供用户确认，绝不会自动发送：
录音时上滑进入 `Cancel` 区域会立即丢弃本次音频，不调用 ASR。Pager 不显示左上角
关闭按钮，返回统一使用系统的左边缘向右滑手势。

```bash
export ZHIPU_API_KEY=...
./scripts/pager_voice_bridge.sh --address BOARD_COREBLUETOOTH_UUID \
  --cache ~/.vibeboard/pager-a.json
```

两块板需要各启动一个进程，并使用不同的 `--address` 与 `--cache`；也可以用
`pager_voice_bridges.sh` 在一个 supervisor 中运行两块板。API key 只保留在
Mac 环境变量或本地忽略文件 `.voice_terminal.env` 中，不进入固件。离线与不计费的
链路测试使用 `--self-test`、`--mock-transcript "语音链路测试"`，或
`--stream-test-seconds 20` 检查长录音流必须为 `dropped=0`；完整说明见
`docs/huangshan-peer-link.md`。

Mac 本地应用商店会打开浏览器页面，列出 `weather_pet`、`game_2048`、`auto_snake` 和
`sensor_stage`。浏览器只访问本机 `127.0.0.1` 服务；服务端通过
`scripts/runtime_transport.py` 选择 BLE 或串口 adapter，再把 Runtime 协议命令发到
黄山派。它不会访问板子的 `192.168.x.x:8080`，也不要求板子提供 HTTP 服务。
服务端会先跑同一套 Runtime 包校验，确认 manifest / `main.lua` / 路径白名单通过后再安装。页面启动时会先读 `/api/runtime/capabilities` 完成 Runtime 能力握手，并显示显示、触摸、传感器、语音、信息流、电源、GPIO、RGB 和安装通道摘要；页面中的“板上 App Manager”是电脑端管理面板：`/api/runtime/apps` 读取已安装 App，`/api/runtime/apps/<appId>/launch`、`/delete` 和 `/api/runtime/apps/stop` 通过同一个 serial/BLE adapter 发送管理命令。它和板子首页卡片读取的是同一份 `/sdcard/apps`，但职责不同：网页负责安装/启动/停止/删除/错误提示，板子首页负责触摸选择和日常启动。服务端用 transport lock 串行化 status、capabilities、install 和 App Manager 操作，并且启动/停止/安装完成后采用本地缓存更新 UI，不立刻连续读取串口状态，避免 LVGL 切屏时被浏览器轮询打断导致屏幕闪烁或回到桌面：

```bash
./scripts/app_store_mac.sh
./scripts/app_store_mac.sh --transport ble
./scripts/app_store_mac.sh --transport ble --ble-name VibeBoard --ble-no-cache
./scripts/app_store_mac.sh --transport serial --serial-port /dev/cu.usbserial-13220
```

使用本地 App Store 时要注意：页面右上角的状态栏只显示摘要，不应该把整段串口日志塞进页面；如果读取 App 列表超时，页面会显示“读取 App 列表超时，请刷新重试”，并保留上一次成功读取的缓存。为了保护板端 LVGL 切屏，Web bridge 不做周期性串口轮询；点击启动后会先乐观更新网页状态，真正的运行状态可用手动“刷新”再确认。

`--ble-name` / `--ble-no-cache` 与 BLE 安装和可靠性脚本使用同一套
`BLETransportOptions`，用于多块板、缓存外设失效或需要强制重新扫描时。

`weather_pet` 安装前会由 Mac 获取实时天气，生成带天气数据的 Runtime App
包，再安装到 `/sdcard/apps/weather_pet`。如果浏览器定位不可用，可在页面里
输入城市兜底。

BLE adapter 会把第一次连接到的外设 identifier/address 缓存在
`~/.vibeboard/huangshan_ble.json`，后续优先重连缓存设备，失败再扫描
`VibeBoard`。App Manager 的 `apps` 读取在 BLE 下使用 `apps_page <offset> <limit>`
小页聚合，当前主机端默认每页 2 个 App，用来避开长 JSON 状态回包截断；串口
adapter 仍使用每页 5 个 App。正式 iPhone App 也按这个模型做自动重连和分页读取；
串口 adapter 则用于开发、工厂和救援场景。

iOS/CoreBluetooth 参考实现位于：

```text
mobile/ios/VibeBoardBLE
```

它提供 `VibeBoardBLEClient.connect()`、`status()`、`capabilities()`、`sensors()`、
`power()`、`display(brightness:)`、`touch()`、`gpio()`、`rgb()`、`sendInfoFlow(...)`、`voice()`、`voiceStatus()`、`voiceStart()`、
`voiceRead()`、`voiceClear()`、`captureVoice(...)`、`sendVoiceReply(...)`、
`install(_:)` 和 `abortInstall(_:)`，以及和串口/BLE 工具一致的 Runtime App 包分块命令生成逻辑。App Manager 的 `app` / `apps` / `launch` / `stop` / `delete` BLE 命令已经封装为 `appStatus()`、`apps()`、`launchApp(...)`、`stopApp()` 和 `deleteApp(...)` 类型化方法。当前
`capabilities()`、`sensors()`、`power()`、`display()`、`touch()`、`gpio()`、`rgb()`、`voice()` 和 `appStatus()` 会优先读取
常规状态回包，必要时自动 fallback 到 `json_read <kind> <offset> <maxBytes>` 分块拉取；`apps()` 则使用 `apps_page <offset> <limit>` 小页连续读取并在主机端聚合，避免分页 offset 语义丢失。这个 Swift package 已经通过 `swift test` 编译和单元测试。

最小 iPhone App 工程位于：

```text
mobile/ios/VibeBoardPhone/VibeBoardPhone.xcodeproj
```

这个 App 只走蓝牙，不连接手机热点，也不需要 Wi-Fi 密码。打开工程并选择
iPhone 运行后，App 会在启动和回到前台时自动连接/重连 `VibeBoard`；界面里
保留了 `Connect / Auto Reconnect`、`Read Runtime Status`、`Read Capabilities`、
`Read Built-in Sensors`、`Read Power`、Display 亮度读写、`Read Touch`、`Read GPIO`、RGB 读写、信息流发送/清空、只读语音 JSON 状态读取、录音桥文本状态读取、
短录音抓取、语音回复回写，以及 `Install Demo App Over BLE`，用于手动验证连接、
Runtime API、语音桥和 App 安装。
App 管理器的状态、列表、启动、停止和删除也已经进入这个 Demo App；删除仍遵循板端规则，需要先停止正在运行的 App。
也可以点 `Import App Folder Over BLE`，从手机“文件”里选择一个 Runtime
App 文件夹并安装；文件夹需要包含 `manifest.json` 或 `app.info`、`main.lua`。
iOS 侧会先按主机脚本同一套规则做离线包校验；安装过程中如果 BLE 命令失败，会和桌面 adapter 一样先尝试 `abortInstall(_:)` 清理 staging 后再把原错误显示出来。如果带 `manifest.json`，当前最低要求是 `kind/id/entry/schemaVersion(version)`
这几个字段能通过 Runtime 校验，组件数量、组件类型/能力白名单、action/status/clock 字段和 `main.lua` 脚本子集也会提前检查；失败原因会直接显示在 Demo App 状态区。
可选资源放在 `assets/`、`images/`、`fonts/` 或 `lib/` 下。
首次连接成功后，手机端会缓存 CoreBluetooth peripheral identifier，后续优先
自动重连，失败才重新扫描 `VibeBoard`。

真机验证前可以先跑本机自检：

```bash
./scripts/verify_phone_ble_ready.sh
```

如果要检查 Xcode 真机签名和 iPhone 可用状态：

```bash
./scripts/verify_phone_device_ready.sh
```

Xcode 账号和 provisioning profile 配好后，可以直接构建、安装并启动手机端：

```bash
./scripts/install_phone_app.sh
```

## 串口监视和复位

macOS / Linux：

```bash
./scripts/monitor.sh /dev/cu.usbserial-13220
```

Windows PowerShell：

```powershell
.\scripts\monitor.ps1 COM7
```

监视脚本会通过 RTS 复位开发板，然后以 `1000000` 波特率抓取启动日志。

## 当前已验证应用

`src/gui_apps/Codex_Test` 是本仓库的第一个自定义内置应用，用于验证 GUI app framework。早期版本可从官方启动器打开；当前 Runtime 产品路径以首页 Runtime App 卡片为主。名称为：

```text
Codex测试 / Codex Test
```

它用于验证：

- 屏幕显示
- 触摸计数
- LVGL 定时器刷新
- 390x450 分辨率显示
- 返回主界面

## 重要 SDK 补丁

当前基于 SDK 2.5.0 构建时，本地 SDK 有一个 LCD framebuffer 兼容补丁：

```text
/Users/hushaohong/vibe-coding/huangshan-pi-workspace/sifli-sdk/rtos/rtthread/bsp/sifli/drivers/drv_lcd_fb.h
/Users/hushaohong/vibe-coding/huangshan-pi-workspace/sifli-sdk/rtos/rtthread/bsp/sifli/drivers/drv_lcd_fb.c
```

补丁提供 `drv_lcd_fb_dump_state(const char *reason)` wrapper，供 SDK 2.5 的
`middleware/lvgl/lv_drivers/lv_lcd.c` 在 `BSP_USING_LCD_FRAMEBUFFER` 路径下调用。
没有这个符号时，LVGL watch/demo 工程会在链接阶段失败。

历史 CO5300 屏幕 bring-up、LCD ID 和 `draw_core timeout` 等问题记录见：

```text
docs/board-bringup.md
```

## 开发原则

本仓库作为黄山派 SF32 的板级应用工作区使用。

新应用、新界面、新板级测试代码优先放在本仓库。只有确实属于底层驱动或
SDK bring-up 的问题，才修改 `sifli-sdk`。

当前 `lvgl/watch` 启动器只是一个已经跑通的应用壳，不是产品形态限制。
后续可以继续保留它，也可以替换成新的 LVGL 壳，或者开发非手表形态的
板级 Demo。
