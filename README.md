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
- 本机验证串口：`/dev/cu.usbserial-110`

## 上游来源

本仓库依赖两个上游项目：

- 官方 SiFli SDK
  - 仓库：https://gitee.com/SiFli/sifli-sdk
  - 分支：`release/v2.4`
  - 本地路径：`/Users/wq/huangshan-pi-workspace/sifli-sdk`

- 立创黄山派例程
  - 仓库：https://github.com/OpenSiFli/lckfb-hspi-ulp_example.git
  - 本地路径：`/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example`

完整 SDK 没有复制进本仓库。它作为外部依赖保留在同一个 workspace
里。当前仓库复制了立创 `lvgl/watch` 的已验证应用结构，用它作为第一版
可运行开发基座。

## 推荐本地目录

建议把 SDK、立创例程和本项目放在同一个目录下：

```text
/Users/wq/huangshan-pi-workspace/
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
./scripts/flash.sh /dev/cu.usbserial-110
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
./scripts/flash_reliability.sh /dev/cu.usbserial-110 --runs 5 --confirm-boot
```

Windows PowerShell：

```powershell
.\scripts\flash.ps1 COM7
```

Windows 串口也可以只传数字，例如 `.\scripts\flash.ps1 7`。烧录脚本使用
UART 下载，需要先完成构建并生成 `bootloader.bin`、`main.bin` 和
`ftab.bin`。

## Runtime App 更新

`VibeBoard_Runtime` 是面向“一次烧录固件，之后不重刷固件更新 App”的
第一版黄山派 Runtime。它会自动启动，也可以从首页 `Runtime` 卡片进入。

Runtime 固件负责屏幕、触摸、文件系统、串口命令和 App 包加载。App 更新
只改逻辑路径：

```text
/sdcard/apps/<appId>/
  manifest.json
  main.lua
  assets/...
/sdcard/apps/.active
```

当前黄山派板型不要求自己联网。第一版通过 Mac Mini 串口桥安装 App：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-110 \
  --package-dir scripts/runtime_apps/clock_test \
  --app-id clock_test
```

安装会先写入隐藏 staging 目录，`install_end` 校验完整包后再提交到
`/sdcard/apps/<appId>`；中途断包不会覆盖当前可运行 App。

验证 10 次免烧录安装/切换：

```bash
./scripts/runtime_reliability.sh /dev/cu.usbserial-110 --runs 10
```

板端 Runtime 暴露的 MSH 命令：

```text
vb_runtime_status
vb_runtime_install_begin <appId>
vb_runtime_install_file <appId> <path> <offset> <hexChunk>
vb_runtime_install_end <appId>
vb_runtime_select <appId>
vb_runtime_reload
vb_runtime_quiet [0|1]
vb_runtime_sensors
vb_runtime_sensor_probe
```

串口日志里应能看到 `[vb_runtime] install complete: <appId>`、
`[vb_runtime] active=<appId>` 和 `transport=serial-msh`。当前版本已经支持
manifest 驱动的 LVGL 展示、App 包落盘，以及 `main.lua` 的安全脚本子集
执行；状态会显示 `lua=script-subset`。这个子集先支持 `print(...)`、基础
LVGL label/button/image、文本/颜色/位置/尺寸、文件读取证据、简单 tick
label，以及 `vibe_sensor_label(label, "light|acce|gyro|mag|step")` 读取板载
传感器首帧快照。Manifest 组件也可以声明 `sensor.light`、`sensor.acce`、
`sensor.gyro`、`sensor.mag`、`sensor.step`，Runtime 会定时刷新显示值。
需要复杂 Lua 语法、闭包定时器、网络、新 LVGL binding 或完整 Lua VM 时，
仍属于 Runtime 固件更新，需要重新烧录。
`vb_runtime_quiet` 默认开启，用来压低自动运行 App 的周期性日志，避免影响
串口或 BLE 安装调试；需要观察游戏 tick 时可执行 `vb_runtime_quiet 0`。

`vb_runtime_sensors` 会初始化 I2C3 上的板载 LTR303 光照、MMC56X3 磁力、
LSM6DSL 加速度/陀螺仪/计步器，并返回一行 JSON，供 AI、手机 App 或 Mac
串口桥直接解析：

```json
{"api":"vibeboard-huangshan-sensors/v1","available":1,"ready":1,"count":1,"light":{"ok":1,"lux":12},"mag":{"ok":1,"x":-389,"y":138,"z":-740},"acce":{"ok":1,"x":0,"y":20,"z":-1009},"gyro":{"ok":1,"x":-1890,"y":-4200,"z":3150},"step":{"ok":1,"count":0}}
```

`ready=1` 表示至少一个板载传感器可读；每个子对象的 `ok` 表示该传感器本次
是否在线并成功读取。若某块板上 `light.ok=0` 或 `mag.ok=0`，可执行
`vb_runtime_sensor_probe` 查看 I2C3 上 LTR303/MMC56X3/LSM6DSL 的应答情况。

安装示例传感器面板：

```bash
./scripts/runtime_install_serial.sh /dev/cu.usbserial-110 \
  --package-dir scripts/runtime_apps/sensor_dash \
  --app-id sensor_dash
```

Runtime 也会默认广播 BLE 设备名 `VibeBoard`，用于后续手机/VibeCoding App
通过 BLE GATT 分块安装 App 包。板端状态命令：

```text
vb_runtime_ble_status
```

当前 BLE 安装服务已验证会返回 `init=1 power=1 service=1 adv=1`。iPhone
不一定会在系统“设置 > 蓝牙”里显示这种自定义 BLE GATT 设备；验证时请用
LightBlue、nRF Connect 或后续 VibeCoding 手机端扫描 `VibeBoard`。

Mac 也可以作为 BLE 中心端验证这条手机同款链路：

```bash
python3 -m venv .venv-ble
.venv-ble/bin/python -m pip install bleak
./scripts/runtime_install_ble.sh --scan-only
./scripts/runtime_install_ble.sh --status-only
./scripts/runtime_install_ble.sh --sensors-only
./scripts/runtime_install_ble.sh \
  --package-dir scripts/runtime_apps/clock_test \
  --app-id clock_test
```

Mac 本地应用商店会打开浏览器页面，列出 `weather_pet`、`auto_snake` 和
`sensor_stage`，并通过同一条 BLE OTA 链路安装到板子：

```bash
./scripts/app_store_mac.sh
```

`weather_pet` 安装前会由 Mac 获取实时天气，生成带天气数据的 Runtime App
包，再 OTA 到 `/sdcard/apps/weather_pet`。如果浏览器定位不可用，可在页面里
输入城市兜底。

BLE 客户端会把第一次连接到的外设 identifier/address 缓存在
`~/.vibeboard/huangshan_ble.json`，后续优先重连缓存设备，失败再扫描
`VibeBoard`。正式 iPhone App 也按这个模型做自动重连。

iOS/CoreBluetooth 参考实现位于：

```text
mobile/ios/VibeBoardBLE
```

它提供 `VibeBoardBLEClient.connect()`、`status()`、`sensors()`、`install(_:)`，以及和
串口/BLE 工具一致的 Runtime App 包分块命令生成逻辑。这个 Swift package
已经通过 `swift test` 编译和单元测试。

最小 iPhone App 工程位于：

```text
mobile/ios/VibeBoardPhone/VibeBoardPhone.xcodeproj
```

这个 App 只走蓝牙，不连接手机热点，也不需要 Wi-Fi 密码。打开工程并选择
iPhone 运行后，App 会在启动和回到前台时自动连接/重连 `VibeBoard`；界面里
仍保留 `Connect / Auto Reconnect`、`Read Runtime Status` 和
`Read Built-in Sensors`、`Install Demo App Over BLE` 用于手动验证连接、
状态读取、传感器读取和 App 安装。
也可以点 `Import App Folder Over BLE`，从手机“文件”里选择一个 Runtime
App 文件夹并安装；文件夹需要包含 `manifest.json` 或 `app.info`、`main.lua`，
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
./scripts/monitor.sh /dev/cu.usbserial-110
```

Windows PowerShell：

```powershell
.\scripts\monitor.ps1 COM7
```

监视脚本会通过 RTS 复位开发板，然后以 `1000000` 波特率抓取启动日志。

## 当前已验证应用

`src/gui_apps/Codex_Test` 是本仓库的第一个自定义应用，已经可以从当前
蜂窝启动器里打开，名称为：

```text
Codex测试 / Codex Test
```

它用于验证：

- 屏幕显示
- 触摸计数
- LVGL 定时器刷新
- 390x450 分辨率显示
- 返回主启动器

## 重要 SDK 补丁

这块板子的 CO5300 屏幕在本机验证时，需要修改官方 SDK 的驱动文件：

```text
/Users/wq/huangshan-pi-workspace/sifli-sdk/customer/peripherals/co5300/co5300.c
```

关键行为：

- 接受 CO5300 读到的 `0x331100`、`0x1fff`、`0x3fff`
- 对当前屏幕路径使用 `HAL_LCDC_SYNC_DISABLE`

没有这个补丁时，曾出现屏幕黑屏、LCD ID 不匹配、`draw_core timeout`
等问题。详细记录见：

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
