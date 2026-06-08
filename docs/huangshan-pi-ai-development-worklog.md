# 黄山派 AI 协作开发实测复盘

日期：2026-06-09

本文记录立创黄山派 / SF32LB52x 在 AI 协作开发中的真实进展、验证证据和后续开发边界。它不是官方资料摘抄，也不是单纯的提交列表，而是给人和 AI 后续接手时使用的工作入口。

核心结论：

- 当前仓库已经能稳定构建、烧录和监视黄山派固件。
- 当前板子已经验证过显示、触摸、应用框架、ADC、GPIO 和板载传感器基础链路。
- 屏幕点亮依赖本地 SiFli SDK 中的 CO5300 适配补丁，不能只看应用层代码。
- 官方独立例程会替换整套固件，测试后必须刷回当前仓库固件。
- VibeBoard 已接入黄山派 workspace，并打通构建、产物识别、本机烧录和串口监视。

## 1. 必须先分清的四层边界

黄山派开发最容易混淆的是“板子”“应用”“官方例程”和“平台”。后续排查问题前，先判断当前改动属于哪一层。

| 层级 | 代表内容 | 关键风险 |
| --- | --- | --- |
| SDK / 板级适配 | LCD、触摸、PSRAM、Flash、pinmux、CO5300 | 屏幕黑屏、LCDC timeout、外设驱动不匹配 |
| 当前应用仓库 | `huangshan-pi-sf32-dev` 的 launcher、内置 app、资源 | app ID、生命周期、SConscript、资源注册 |
| 官方独立例程 | 立创 / SiFli 的 ADC、GPIO、Sensor、LVGL demo | 刷入后会替换当前应用固件 |
| VibeBoard 平台 | Web workspace、构建服务、产物、烧录、串口监视 | 服务器能编译，但只有本机能看到本机 USB 串口 |

这四层不要混写结论。尤其不要把“官方例程构建通过”说成“当前应用页面已集成”，也不要把“服务器编译成功”说成“本机板子已烧录成功”。

## 2. 当前工作区和硬件事实

本轮实测使用的路径：

```text
当前应用仓库：
/Users/wq/huangshan-pi-workspace/huangshan-pi-sf32-dev

SiFli SDK：
/Users/wq/huangshan-pi-workspace/sifli-sdk

立创官方例程：
/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example

VibeBoard：
/Users/wq/VibeBoard
```

本轮实测使用的硬件和目标：

```text
开发板：立创黄山派 / LCKFB Huangshan Pi
芯片系列：SF32LB52x
构建目标：sf32lb52-lchspi-ulp
屏幕驱动 IC：CO5300
触摸芯片：FT6146
调试串口：/dev/cu.usbserial-110
monitor.py 串口波特率：1000000
VibeBoard 串口监视默认波特率：921600
```

波特率说明：

- `scripts/monitor.sh` 使用 Python `pyserial`，实测可用 `1000000`。
- VibeBoard 后端用 macOS `stty` 配置串口，`1000000` 不可用，已改用 `921600`。
- 如果后续统一监视实现，应优先复用 Python / pyserial 路径，减少 `stty` 平台差异。

多个成功启动的 GUI 固件都出现过这些日志：

```text
SFBL
Serial:c2,Chip:4,Package:3,Rev:3
Found lcd co5300 id:331100h
touch screen found driver ..., ft6146
[littlevgl2rtt] Welcome to the littlevgl2rtt lib.
display on
```

这组日志是判断板级基础链路是否正常的第一证据。

## 3. 当前可复用命令

当前应用仓库的基础闭环：

```bash
cd /Users/wq/huangshan-pi-workspace/huangshan-pi-sf32-dev

./scripts/build.sh
./scripts/flash.sh /dev/cu.usbserial-110
SECONDS_TO_CAPTURE=10 ./scripts/monitor.sh /dev/cu.usbserial-110
```

对应关系：

- `build.sh`：进入 `project/`，source SiFli SDK，再执行 `scons --board=sf32lb52-lchspi-ulp -j8`。
- `flash.sh`：进入生成目录，调用 `uart_download.sh`。
- `monitor.sh`：使用 pyserial 打开调试串口并采集启动日志。

这三条命令是后续每次改动后的最小真板验证闭环。

## 4. 板级点亮：CO5300 是关键前置条件

这块板子的屏幕是 CO5300。实测中遇到过两个关键问题。

屏幕 ID 识别失败：

```text
Try lcd co5300, read id:1fffh, expect:331100h
unknow lcd!
```

LCDC 同步超时：

```text
draw_core timeout
LCDC STATUS=1,TE=3
[INITIALIZED] -> [TIMEOUT]
```

当前本地 SDK 中与屏幕相关的重要适配在：

```text
/Users/wq/huangshan-pi-workspace/sifli-sdk/customer/peripherals/co5300/co5300.c
```

当前接受的 CO5300 ID：

```text
0x331100
0x1fff
0x3fff
```

当前已验证的同步模式：

```c
.syn_mode = HAL_LCDC_SYNC_DISABLE,
```

结论：黄山派开发不能只看应用层 UI 代码。屏幕能否点亮，首先依赖 SDK 板级外设驱动对当前屏幕模组的识别和 LCDC 同步方式。

## 5. 当前应用仓库已经完成的工作

### 5.1 开发基线

当前仓库不是空项目，而是基于 SiFli / 立创 watch 示例体系整理出的应用工作区。

已经确认三条基础链路：

- 源码可以构建。
- 固件可以通过 CH340 串口烧录。
- 串口可以捕获板上启动和应用日志。

这一步的意义是：后续每次开发都能从源码到真板子验证，避免只停留在“编译看起来没问题”。

### 5.2 Board Diagnostics 应用

新增板级诊断页面：

```text
src/gui_apps/Board_Diagnostics/
```

主要文件：

```text
src/gui_apps/Board_Diagnostics/main.c
src/gui_apps/Board_Diagnostics/SConscript
```

该应用目标不是产品功能，而是确认基础交互能力：

- LVGL 页面能创建并显示。
- 触摸事件能进入 LVGL 回调。
- KEY1 / KEY2 GPIO 能读取。
- 定时器能稳定更新 UI。
- 串口能持续输出调试日志。
- 应用能通过 watch app framework 注册、启动、暂停、停止。

注册方式：

```c
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(board_diagnostics), LV_EXT_IMG_GET(img_LiChuang), APP_ID, app_main);
```

启动证据：

```text
[Board_Diagnostics] registered
[Board_Diagnostics] start
[Board_Diagnostics] resume
```

触摸证据：

```text
[Board_Diagnostics] touch count=1 x=... y=...
```

按键证据：

```text
[Board_Diagnostics] KEY1 GPIO34 ... ...
[Board_Diagnostics] KEY2 GPIO43 ... ...
```

关键经验：

- 新增内置 GUI 应用需要独立目录、`SConscript`、应用 ID、消息处理函数和 `BUILTIN_APP_EXPORT`。
- watch app framework 分发生命周期，应用自身要处理 `ONSTART`、`ONRESUME`、`ONPAUSE`、`ONSTOP`。
- `ONSTOP` 中要释放 LVGL timer 和 root object，否则反复进入页面可能积累对象或回调。

### 5.3 避免诊断页误控 GPIO26

早期诊断思路中曾尝试加入 LED / GPIO 控制，后来移除了 Diagnostics 对 LED GPIO 的主动控制，只保留按键和触摸等低风险读取路径。

相关提交：

```text
fdf1543 fix: avoid diagnostics LED GPIO control
```

经验：板级辅助应用不要随便驱动不确定用途的 GPIO。一个 GPIO 可能牵涉背光、电源、外设使能或保留功能。没有明确原理图和官方例程证据前，应优先读状态，不主动写电平。

### 5.4 Huangshan Home 首页

当前启动器已改造成更明确的 Huangshan Home：

```text
src/gui_apps/main/app_mainmenu.c
```

恢复到当前应用固件后，串口确认：

```text
[Huangshan_Home] start
```

这条日志是“板子已经回到当前仓库应用固件”的关键证据。官方独立例程刷入后会替换整套固件，所以每轮官方例程测试结束后，都要刷回当前仓库固件并确认这条日志。

## 6. 官方 watch 内置应用实测

当前仓库中保留了一批官方 watch app 示例：

```text
src/gui_apps/LC_Hello_World/
src/gui_apps/rotation3d/
src/gui_apps/clock/
src/gui_apps/mem/
src/gui_apps/utils/
src/gui_apps/watch_demo.c
```

这些代码主要用于学习 SiFli 的 GUI 应用框架和 LVGL 用法，不是我们自己的产品页面。

本轮通过串口命令测试过：

```text
app_run hello_world
app_run Main
app_run rotation3d
app_run Main
app_run clock
app_run Main
```

实测结论：

| 应用 ID | 结果 | 适合学习什么 |
| --- | --- | --- |
| `hello_world` | 通过 | 最小内置 GUI 应用结构 |
| `rotation3d` | 通过 | LVGL timer、动画和销毁流程 |
| `clock` | 通过 | 多模式状态管理、页面切换和资源释放 |

典型生命周期日志：

```text
send msg[GUI_APP_MSG_RUN_APP] [hello_world]
app[hello_world] do LOAD
finding hello_world in builtin apps...
page[hello_world][root] do ONSTART
page[hello_world][root] do ONRESUME
page[hello_world][root] do ONSTOP
```

后续学习建议：

- 写第一个新页面时先参考 `hello_world`。
- 做动画或周期刷新时参考 `rotation3d`，重点看 timer 清理。
- 做复杂页面状态切换时参考 `clock`，重点看 pause / resume / deinit 顺序。

## 7. 官方独立例程实测

官方独立例程来自：

```text
/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example
```

它们和当前 `Huangshan Home` 不是同一个固件。刷入任意官方独立例程，都会替换当前板子上的应用。

官方独立例程构建命令：

```bash
source /Users/wq/huangshan-pi-workspace/sifli-sdk/export.sh
scons --board=sf32lb52-lchspi-ulp -j8
```

官方独立例程刷机命令：

```bash
source /Users/wq/huangshan-pi-workspace/sifli-sdk/export.sh
./uart_download.sh /dev/cu.usbserial-110
```

本轮矩阵：

| 项目 | 构建结果 | 板上结果 | 学习价值 |
| --- | --- | --- | --- |
| `adc/project` | 通过 | 通过 | ADC 设备、VBAT、PA34 采样 |
| `gpio/project` | 通过 | 通过 | GPIO 编号、输出读回、按键中断 |
| `uart/project` | 通过 | 未完整测试 | UART2，需要外接 USB-TTL |
| `I2C/charger/project` | 通过 | 未刷机 | AW32001 充电芯片，有硬件副作用 |
| `RT-Device/sensor/project` | 通过 | 通过 | I2C3、光照、磁力、IMU、计步器 |
| `ws2812/project` | 通过 | 未完整测试 | 需要外接 WS2812 灯珠 |
| `lvgl/lvgl_v8_demos/project` | 通过 | 串口初始化通过 | 独立 LVGL v8 主循环 |
| `lvgl/lvgl_v9_demos/project` | 通过 | 串口 / sysmon 通过 | LVGL v9 API 和性能输出 |

### 7.1 ADC 例程

串口证据：

```text
Start adc demo!
ch[7]voltage=43504;
VBAT read value: 43504
adc control origin data 825, Voltage 109
ADC read value: 109
ADC example end
```

观察到的范围：

```text
VBAT 原始值约 43104 到 43674
PA34 ADC 电压值约 777 到 893 mV
```

学习点：

- ADC1 在 RT-Thread 设备框架中暴露为 `bat1`。
- 例程使用 channel 7 读取 VBAT，使用 channel 6 读取 PA34。
- ADC 数值受供电、电池、按键状态影响，应看“合理且变化”，不要把某个数值当成固定标准。

### 7.2 GPIO 例程

串口证据：

```text
GPIO example
PIN 20 state: 1
PIN 20 state: 0
GPIO example end
Waiting
KEY2 pressed
KEY2 released
```

学习点：

- 对当前芯片系列来说，GPIO 编号可以直接对应 PA 编号。
- PA20 可以作为简单输出例子。
- KEY2 是 PA43，可以通过 `rt_pin_attach_irq` 处理中断。
- 中断回调里应只做很小的工作，例如记录状态或打印日志。

### 7.3 Sensor 例程

本轮刷入并运行了官方 `RT-Device/sensor/project`。

初始化证据：

```text
Hello world!
[D/drv.als] Find i2c bus device i2c3
LTR303_MEAS_RATE Reg[0] = 3
[I/sensor.ltr-303] light sensor init success
MMC56x3 ID = 16
[I/sensor.mmc56x3] mag sensor init success
[I/sensor.st.lsm6dsl] sensor init success
acce set odr 1660
gyro set odr 1660
```

数据输出样例：

```text
light: 12 lux
mag, x: -389, y: 138, z: -740
acce, x: 0, y: 20, z: -1009
gyro, x: -1890, y: -4200, z: 3150
lsm6d step, step: 0
```

学习点：

- Sensor 例程使用 I2C3。
- I2C3 对应 PA40 / PA39 作为 SCL / SDA。
- LTR303 光照传感器可以初始化并持续输出 `light`。
- MMC56X3 磁力传感器可以初始化并持续输出三轴 `mag`。
- LSM6DSL 加速度计和陀螺仪可以初始化并持续输出 `acce` / `gyro`。
- 静置时计步器 `step` 保持 0 是合理现象；后续要通过移动或步行动作单独验证动态计步。

这个例程是后续做“传感器仪表盘”的最重要官方参考。

### 7.4 LVGL v8 / v9 独立 demo

两套 LVGL demo 都能初始化显示和触摸。

典型证据：

```text
Found lcd co5300 id:331100h
touch screen found driver ..., ft6146
[littlevgl2rtt] Welcome to the littlevgl2rtt lib.
display on
```

LVGL v9 还会持续输出 sysmon：

```text
sysmon: 62 FPS ...
sysmon: 39 FPS ...
```

学习点：

- 独立 LVGL demo 不走 watch app framework。
- 它们适合学习 LVGL 主循环、demo 选择、性能监控。
- 如果继续沿用当前 `Huangshan Home`，新增页面应优先学习 watch app framework 的应用注册和生命周期。

## 8. 未完整测试项和原因

### 8.1 UART2

状态：构建通过，未完整板测。

原因：

- 官方 UART 例程使用 UART2。
- UART2 不是当前调试串口。
- 完整测试需要额外 USB-TTL 模块。

硬件映射：

```text
UART2 TX：PA19
UART2 RX：PA18
```

完整测试至少需要两条串口路径：

- 当前调试串口看系统日志。
- 外接 USB-TTL 接 UART2 做收发。

### 8.2 I2C Charger

状态：构建通过，未刷机。

原因：

- 该例程访问 AW32001 充电芯片。
- 会扫描 I2C、读取芯片 ID。
- 还会循环写入充电电流寄存器。

这是有真实硬件副作用的例程。后续要确认供电、电池和安全边界后再运行。

### 8.3 WS2812

状态：构建通过，未完整板测。

原因：

- 需要外接 WS2812 灯珠。
- 串口只能证明程序进入颜色循环，不能证明灯珠接线、时序和颜色实际正确。

## 9. 最终恢复状态

官方独立例程测试结束后，已经刷回当前应用仓库固件。

恢复命令：

```bash
./scripts/flash.sh /dev/cu.usbserial-110
```

恢复后串口证据：

```text
Found lcd co5300 id:331100h
touch screen found driver ..., ft6146
display on
[Huangshan_Home] start
```

因此当前板子最终状态不是官方 ADC、GPIO、Sensor 或 LVGL 独立例程，而是我们自己的 `Huangshan Home` 固件。

## 10. VibeBoard 平台集成状态

VibeBoard 已经把黄山派作为独立 workspace 接入，而不是强行塞进 ESP-IDF board selector。

当前已完成：

- 黄山派 board profile。
- Huangshan Workspace UI。
- 黄山派 app 模板生成。
- SiFli/SCons 构建服务。
- 构建日志解析和 build evidence。
- 构建产物识别：`main.bin`、`sftool_param.json`、`download.bat`。
- 本机串口枚举。
- 本机烧录。
- 本机串口监视。

关键 VibeBoard 提交：

```text
e1b70cd Add Huangshan workspace UI
358bd46 Add Huangshan build artifact summary
9bc97d3 Add Huangshan local flash controls
c09f684 Add Huangshan serial monitor
```

服务器侧已经配置：

```text
SiFli SDK：
/home/wq/huangshan-pi-workspace/sifli-sdk

黄山派工程：
/home/wq/huangshan-pi-workspace/huangshan-pi-sf32-dev

VibeBoard 服务：
vibeboard-huangshan.service
```

服务器能做：

- 编译黄山派工程。
- 解析构建产物。
- 服务前端页面。

服务器不能直接做：

- 烧录插在用户 Mac 上的黄山派。
- 读取用户 Mac 上的 `/dev/cu.usbserial-110`。

原因很简单：USB 串口是本机设备，不在服务器上。

因此当前正确使用方式是：

- 线上 VibeBoard：适合验证服务器构建和平台入口。
- 本机 VibeBoard 服务：适合直接烧录和串口监视已连接设备。

本机服务端口：

```text
http://127.0.0.1:8771
```

本机串口识别证据：

```text
{"ports":[{"path":"/dev/cu.usbserial-110","recommended":true}]}
```

本机烧录命令等价于：

```bash
sftool -p /dev/cu.usbserial-110 -c SF32LB52 -m nor write_flash \
  bootloader/bootloader.bin@0x12010000 \
  main.bin@0x12020000 \
  ftab/ftab.bin@0x12000000
```

本机串口监视使用：

```bash
stty -f /dev/cu.usbserial-110 921600 raw -echo
```

## 11. 后续深度学习路线

### 第一阶段：应用框架

目标：稳定新增一个自己的页面。

重点文件：

```text
src/gui_apps/LC_Hello_World/main.c
src/gui_apps/Board_Diagnostics/main.c
src/gui_apps/main/app_mainmenu.c
src/gui_apps/SConscript
src/resource/strings/zh_cn.json
src/resource/strings/en_us.json
```

要掌握：

- app ID。
- `BUILTIN_APP_EXPORT`。
- `SConscript`。
- `GUI_APP_MSG_ONSTART` / `ONRESUME` / `ONPAUSE` / `ONSTOP`。
- LVGL object 创建和释放。
- 从页面返回 `Main`。

### 第二阶段：传感器路径

目标：做一个自己的传感器仪表盘。

优先参考：

```text
/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example/RT-Device/sensor
```

要掌握：

- I2C3 初始化。
- LTR303 光照读取。
- MMC56X3 磁力读取。
- LSM6DSL 加速度 / 陀螺仪读取。
- RT-Thread sensor device 的 open / read / control 流程。
- 如何把传感器数据接入 LVGL 页面。

### 第三阶段：输入和按键

目标：把触摸、KEY1、KEY2 做成可靠交互输入。

优先参考：

```text
src/gui_apps/Board_Diagnostics/main.c
/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example/gpio
```

要掌握：

- `rt_pin_mode`。
- `rt_pin_read`。
- `rt_pin_attach_irq`。
- active high / active low。
- 中断回调和 UI 更新之间的边界。

### 第四阶段：显示和性能

目标：理解屏幕刷新、LVGL 性能和黑屏排查。

优先参考：

```text
docs/board-bringup.md
/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example/lvgl/lvgl_v8_demos
/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example/lvgl/lvgl_v9_demos
```

要掌握：

- CO5300 ID。
- `HAL_LCDC_SYNC_DISABLE`。
- `display on` 日志。
- LVGL sysmon FPS。
- 黑屏时从 LCD driver、LCDC timeout、LVGL 初始化三层排查。

### 第五阶段：高风险外设

目标：在明确硬件边界后再测 UART2、WS2812、charger。

建议顺序：

1. UART2：准备 USB-TTL，接 PA19 / PA18 / GND。
2. WS2812：准备灯珠，确认供电和数据脚。
3. Charger：确认电池、供电、安全边界后再运行。

## 12. 后续 AI 开发规则

AI 后续继续开发时必须遵守这些规则：

1. 先判断当前要改的是官方例程、当前应用、SDK 板级适配，还是 VibeBoard 平台。
2. 不要把官方独立例程当成当前应用页面。
3. 不要把构建通过说成板上通过。
4. 每次刷官方独立例程后，都要刷回当前应用固件并确认 `[Huangshan_Home] start`。
5. 涉及 GPIO、charger、电源、背光、外设使能时，先查官方例程和原理图，不要盲目写电平。
6. 所有硬件结论都要留下串口证据、命令、路径或提交号。
7. 平台侧要区分服务器能力和本机 USB 串口能力。
8. 如果测试结果会影响后续学习路径，要更新 `docs/huangshan-pi-official-examples-report.md` 或本文档。

## 13. 关键提交索引

当前应用仓库关键提交：

```text
0579391 feat: add board diagnostics app
83963e2 feat: add board signal monitor
6f43513 feat: add huangshan home launcher
fdf1543 fix: avoid diagnostics LED GPIO control
a640612 docs: record official example test results
3613005 docs: add official examples test report
b123a3e docs: add sensor example board results
5439a0a docs: add ai development worklog
```

VibeBoard 平台关键提交：

```text
e1b70cd Add Huangshan workspace UI
358bd46 Add Huangshan build artifact summary
9bc97d3 Add Huangshan local flash controls
c09f684 Add Huangshan serial monitor
```

提交含义：

- `0579391` / `83963e2`：建立自己的板级诊断页面。
- `6f43513`：建立 Huangshan Home 首页。
- `fdf1543`：收敛诊断页硬件风险，避免误控 LED GPIO。
- `a640612`：记录官方 watch 和独立例程测试笔记。
- `3613005`：整理完整中文官方例程测试报告。
- `b123a3e`：补充 sensor 官方例程真实板测结果。
- VibeBoard `e1b70cd` 到 `c09f684`：把黄山派接入平台，并打通编译、产物、烧录、监视。

## 14. 一句话总结

当前黄山派已经具备稳定的显示、触摸、应用框架、ADC、GPIO、板载传感器和平台化编译 / 烧录 / 串口监视基础。后续开发应在这些已验证路径上继续推进传感器仪表盘、输入交互、BLE 控制和低功耗实验。
