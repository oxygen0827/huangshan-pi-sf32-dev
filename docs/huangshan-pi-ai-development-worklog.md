# 黄山派 AI 协作开发实测复盘

日期：2026-06-09

## 1. 文档目的

这份文档记录本轮围绕立创黄山派 / SF32LB52x 开发板实际做过的工作。它不是官方资料摘抄，也不是单纯的功能清单，而是给后续继续开发和让 AI 理解这块板子使用的实测复盘。

阅读这份文档时要注意四个边界：

- 官方例程：来自立创 / SiFli 的 watch、ADC、GPIO、Sensor、LVGL 等示例。
- 当前仓库应用：`huangshan-pi-sf32-dev` 里我们自己维护的固件和 UI。
- 本地 SDK 适配：为了让这块板子的 CO5300 屏幕稳定点亮，对本地 SiFli SDK 做过必要补丁。
- 未完整测试项：构建通过不等于板上功能完整通过，涉及外设或硬件副作用的例程仍需单独测试。

## 2. 当前工作区和硬件事实

本轮实测使用的路径：

```text
当前应用仓库：
/Users/wq/huangshan-pi-workspace/huangshan-pi-sf32-dev

SiFli SDK：
/Users/wq/huangshan-pi-workspace/sifli-sdk

立创官方例程：
/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example
```

本轮实测使用的硬件和目标：

```text
开发板：立创黄山派 / LCKFB Huangshan Pi
芯片系列：SF32LB52x
构建目标：sf32lb52-lchspi-ulp
屏幕驱动 IC：CO5300
触摸芯片：FT6146
调试串口：/dev/cu.usbserial-110
串口波特率：1000000
```

多个成功启动的 GUI 固件都出现过这些日志：

```text
SFBL
Serial:c2,Chip:4,Package:3,Rev:3
Found lcd co5300 id:331100h
touch screen found driver ..., ft6146
[littlevgl2rtt] Welcome to the littlevgl2rtt lib.
display on
```

这些日志是后续判断板级基础链路是否正常的第一组证据。

## 3. 本轮实际做过的工作

### 3.1 建立当前应用仓库的开发基线

当前仓库不是一个空项目，而是基于 SiFli / 立创 watch 示例体系整理出来的应用工作区。我们先确认了构建、烧录、串口监视三条链路：

```bash
./scripts/build.sh
./scripts/flash.sh /dev/cu.usbserial-110
SECONDS_TO_CAPTURE=10 ./scripts/monitor.sh /dev/cu.usbserial-110
```

这一步的意义是：后续每次开发都能用同一套命令从源码到真板子验证，避免只停留在“编译看起来没问题”。

### 3.2 修通屏幕点亮路径

这块板子的屏幕是 CO5300。实测中遇到过两个关键问题：

```text
Try lcd co5300, read id:1fffh, expect:331100h
unknow lcd!
```

以及：

```text
draw_core timeout
LCDC STATUS=1,TE=3
[INITIALIZED] -> [TIMEOUT]
```

当前本地 SDK 中与屏幕相关的重要适配在：

```text
/Users/wq/huangshan-pi-workspace/sifli-sdk/customer/peripherals/co5300/co5300.c
```

本地 SDK 接受的 CO5300 ID 包括：

```text
0x331100
0x1fff
0x3fff
```

并且当前已验证屏幕路径使用：

```c
.syn_mode = HAL_LCDC_SYNC_DISABLE,
```

这说明黄山派开发时不能只看应用层 UI 代码。屏幕能否点亮，首先依赖 SDK 板级外设驱动对当前屏幕模组的识别和同步方式。

### 3.3 新增 Board Diagnostics 应用

我们新增了一个板级诊断页面：

```text
src/gui_apps/Board_Diagnostics/
```

主要文件：

```text
src/gui_apps/Board_Diagnostics/main.c
src/gui_apps/Board_Diagnostics/SConscript
```

该应用的目标不是产品功能，而是帮助我们确认板子的基础交互能力：

- LVGL 页面能创建并显示；
- 触摸事件能进入 LVGL 回调；
- KEY1 / KEY2 GPIO 能读取；
- 定时器能稳定更新 UI；
- 串口能持续输出调试日志；
- 应用能通过 watch app framework 注册、启动、暂停、停止。

诊断应用注册方式：

```c
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(board_diagnostics), LV_EXT_IMG_GET(img_LiChuang), APP_ID, app_main);
```

诊断应用启动时会输出：

```text
[Board_Diagnostics] registered
[Board_Diagnostics] start
[Board_Diagnostics] resume
```

触摸时会输出类似：

```text
[Board_Diagnostics] touch count=1 x=... y=...
```

按键电平变化时会输出类似：

```text
[Board_Diagnostics] KEY1 GPIO34 ... ...
[Board_Diagnostics] KEY2 GPIO43 ... ...
```

这一步学到的关键点：

- 新增内置 GUI 应用时，需要独立目录、`SConscript`、应用 ID、消息处理函数和 `BUILTIN_APP_EXPORT`。
- watch app framework 会负责应用生命周期分发，应用自身要处理 `ONSTART`、`ONRESUME`、`ONPAUSE`、`ONSTOP`。
- `ONSTOP` 中要释放 LVGL timer 和 root object，否则反复进入页面可能积累对象或回调。

### 3.4 避免诊断页误控 GPIO26

早期诊断思路中曾尝试加入 LED / GPIO 控制，但实际板级验证中发现这会带来风险。后来提交中移除了 Diagnostics 对 LED GPIO 的控制，只保留按键和触摸等低风险读取路径。

相关提交：

```text
fdf1543 fix: avoid diagnostics LED GPIO control
```

这一步的经验是：板级辅助应用不要随便驱动不确定用途的 GPIO。对 MCU 板子来说，一个 GPIO 可能同时牵涉背光、电源、外设使能或保留功能。没有明确原理图和官方例程证据前，应优先读状态，不主动写电平。

### 3.5 新增 Huangshan Home 首页

我们改造了当前启动器：

```text
src/gui_apps/main/app_mainmenu.c
```

目标是让板子开机后进入更明确的 Huangshan Home，而不是只停留在官方示例默认样式里。实测恢复到当前应用固件后，串口确认：

```text
[Huangshan_Home] start
```

这条日志是当前板子已经回到我们自己应用固件的关键证据。因为官方独立例程刷入后会替换整套固件，所以每轮官方例程测试结束后，都要刷回当前仓库固件并确认这条日志。

## 4. 官方 watch 内置应用实测

当前仓库中保留了一批官方 watch app 示例：

```text
src/gui_apps/LC_Hello_World/
src/gui_apps/rotation3d/
src/gui_apps/clock/
src/gui_apps/mem/
src/gui_apps/utils/
src/gui_apps/watch_demo.c
```

这些代码主要用于学习 SiFli 的 GUI 应用框架和 LVGL 用法。它们不是我们自己开发的产品页面。

本轮在当前应用固件中通过串口命令测试过：

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
- 做动画或周期刷新时参考 `rotation3d`，但要重点看 timer 清理。
- 做复杂页面状态切换时参考 `clock`，尤其是 pause / resume / deinit 的顺序。

## 5. 官方独立例程实测

官方独立例程来自：

```text
/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example
```

这些项目和当前 `Huangshan Home` 不是同一个固件。刷入任意官方独立例程，都会替换当前板子上的应用。因此测试结束后必须刷回当前仓库固件。

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

### 5.1 ADC 例程

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
- ADC 数值受供电、电池、按键状态影响，测试时应看“合理且变化”，不要把某个数值当成固定标准。

### 5.2 GPIO 例程

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

### 5.3 Sensor 例程

本轮最新实测中，刷入并运行了官方 `RT-Device/sensor/project`。

启动和初始化证据：

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
- I2C3 对应的源码配置是 PA40 / PA39 作为 SCL / SDA。
- LTR303 光照传感器可以初始化并持续输出 `light`。
- MMC56X3 磁力传感器可以初始化并持续输出三轴 `mag`。
- LSM6DSL 加速度计和陀螺仪可以初始化并持续输出 `acce` / `gyro`。
- 静置测试中计步器 `step` 保持 0，这是合理现象；后续要通过移动或步行动作单独验证动态计步。

这个例程是后续做“传感器仪表盘”最重要的官方参考。

### 5.4 LVGL v8 / v9 独立 demo

两套 LVGL demo 都能初始化显示和触摸。典型证据：

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
- 但后续如果继续沿用当前 `Huangshan Home`，新增页面更应该优先学习 watch app framework 的应用注册和生命周期。

## 6. 未完整测试项和原因

### 6.1 UART2

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

完整测试时至少需要两条串口路径：

- 当前调试串口看系统日志；
- 外接 USB-TTL 接 UART2 做收发。

### 6.2 I2C Charger

状态：构建通过，未刷机。

原因：

- 该例程访问 AW32001 充电芯片；
- 会扫描 I2C、读取芯片 ID；
- 还会循环写入充电电流寄存器。

这是有真实硬件副作用的例程，后续要确认供电、电池和安全边界后再运行。

### 6.3 WS2812

状态：构建通过，未完整板测。

原因：

- 需要外接 WS2812 灯珠。
- 串口只能证明程序进入颜色循环，不能证明灯珠接线、时序和颜色实际正确。

## 7. 最终恢复状态

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

## 8. 对后续深度学习最有价值的结论

### 8.1 先分清“板子”和“应用”

黄山派开发里最容易混淆的是：屏幕、触摸、Flash、PSRAM、GPIO、I2C、UART 这些是板级事实；页面、按钮、传感器展示、蓝牙服务这些是应用逻辑。

后续遇到问题时要先判断属于哪层：

```text
屏幕不亮        -> SDK / board / LCD driver / TE / panel ID
触摸没反应      -> touch driver / I2C / LVGL input
应用打不开      -> app ID / BUILTIN_APP_EXPORT / SConscript / lifecycle
传感器没数据    -> I2C bus / sensor driver / RT-Thread device name
UART 没收发     -> UART instance / pin mux / external wiring
```

### 8.2 构建通过不是板上通过

本轮已经明确区分：

- 构建通过：`scons` 能生成 `main.bin`。
- 串口初始化通过：固件能启动并初始化关键驱动。
- 板上功能通过：真实硬件数据或交互行为被观察到。
- 未完整测试：缺少外设或存在硬件副作用，不能假装通过。

这个分类后续必须继续保持，否则 AI 很容易把“编译成功”误认为“硬件功能成功”。

### 8.3 每轮官方例程测试后都要刷回当前固件

官方独立例程是完整固件，不是当前应用里的页面。测试顺序应该是：

```text
构建官方例程
刷入官方例程
串口记录证据
判断通过 / 部分通过 / 未通过
刷回当前仓库固件
串口确认 [Huangshan_Home] start
更新文档
```

这能避免一个常见混乱：以为板子还在运行我们自己的应用，实际已经停在某个官方 demo 固件。

### 8.4 官方例程应该被当成“最小硬件证据”

官方 ADC、GPIO、Sensor 例程的价值不是界面好看，而是它们给出了最小可运行硬件路径：

- ADC：证明 `bat1`、channel 7、channel 6 能读。
- GPIO：证明 PA20 输出、PA43 按键中断能用。
- Sensor：证明 I2C3、LTR303、MMC56X3、LSM6DSL 能用。
- LVGL：证明 LCD、触摸、framebuffer、LVGL 能启动。

后续我们自己的应用应该先复用这些最小路径，再做 UI 和业务逻辑。

## 9. 建议的下一阶段学习顺序

### 第一阶段：应用框架

目标：能稳定新增一个自己的页面。

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

- app ID；
- `BUILTIN_APP_EXPORT`；
- `SConscript`；
- `GUI_APP_MSG_ONSTART` / `ONRESUME` / `ONPAUSE` / `ONSTOP`；
- LVGL object 创建和释放；
- 从页面返回 `Main`。

### 第二阶段：传感器路径

目标：做一个自己的传感器仪表盘。

优先参考：

```text
/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example/RT-Device/sensor
```

要掌握：

- I2C3 初始化；
- LTR303 光照读取；
- MMC56X3 磁力读取；
- LSM6DSL 加速度 / 陀螺仪读取；
- RT-Thread sensor device 的 open / read / control 流程；
- 如何把传感器数据接入 LVGL 页面。

### 第三阶段：输入和按键

目标：把触摸、KEY1、KEY2 做成可靠交互输入。

优先参考：

```text
src/gui_apps/Board_Diagnostics/main.c
/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example/gpio
```

要掌握：

- `rt_pin_mode`；
- `rt_pin_read`；
- `rt_pin_attach_irq`；
- active high / active low；
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

- CO5300 ID；
- `HAL_LCDC_SYNC_DISABLE`；
- `display on` 日志；
- LVGL sysmon FPS；
- 黑屏时从 LCD driver、LCDC timeout、LVGL 初始化三层排查。

### 第五阶段：高风险外设

目标：在明确硬件边界后再测 UART2、WS2812、charger。

顺序建议：

1. UART2：准备 USB-TTL，接 PA19 / PA18 / GND。
2. WS2812：准备灯珠，确认供电和数据脚。
3. Charger：确认电池、供电、安全边界后再运行。

## 10. 相关提交

本轮关键提交：

```text
0579391 feat: add board diagnostics app
83963e2 feat: add board signal monitor
6f43513 feat: add huangshan home launcher
fdf1543 fix: avoid diagnostics LED GPIO control
a640612 docs: record official example test results
3613005 docs: add official examples test report
b123a3e docs: add sensor example board results
```

这些提交对应的实际含义：

- `0579391` / `83963e2`：建立自己的板级诊断页面。
- `6f43513`：建立自己的 Huangshan Home 首页。
- `fdf1543`：收敛诊断页硬件风险，避免误控 LED GPIO。
- `a640612`：记录官方 watch 和独立例程测试笔记。
- `3613005`：整理完整中文官方例程测试报告。
- `b123a3e`：补充 sensor 官方例程真实板测结果。

## 11. 后续 AI 使用这份文档的方式

后续继续开发时，AI 应该遵守这些规则：

1. 先判断当前要改的是官方例程、当前应用，还是 SDK 板级适配。
2. 不要把官方独立例程当成当前应用页面。
3. 不要把构建通过说成板上通过。
4. 每次刷官方独立例程后，都要刷回当前应用固件并确认 `[Huangshan_Home] start`。
5. 涉及 GPIO、charger、电源、背光、外设使能时，先查官方例程和原理图，不要盲目写电平。
6. 所有硬件结论都要留下串口证据、命令、路径或提交号。
7. 如果测试结果会影响后续学习路径，要更新 `docs/huangshan-pi-official-examples-report.md` 或本复盘文档。

这份复盘的核心结论是：当前黄山派已经具备稳定的显示、触摸、应用框架、ADC、GPIO 和板载传感器实测基础。后续可以在这些已验证路径上继续做更复杂的 UI、传感器仪表盘、BLE 控制器和低功耗实验。
