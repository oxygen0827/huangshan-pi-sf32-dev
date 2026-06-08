# 黄山派官方例程实测报告

日期：2026-06-09

## 1. 报告目的

这份报告整理本轮在立创黄山派上的实际测试结果。目标不是简单判断“例程能不能跑”，而是让后续开发者和 AI 能更清楚地理解：

- 哪些代码来自立创 / SiFli 官方例程；
- 哪些代码是我们自己开发或改造出来的；
- 哪些例程已经在真板子上验证；
- 哪些例程只是构建通过，但因为缺少外设或存在硬件副作用，没有做完整板上测试；
- 从这些例程里可以提炼出哪些应用开发经验。

## 2. 测试环境

硬件与目标板：

- 开发板：立创黄山派 / LCKFB Huangshan Pi
- 构建目标：`sf32lb52-lchspi-ulp`
- 芯片系列：SF32LB52x
- 已验证 LCD：CO5300
- 已验证触摸芯片：FT6146
- 本机串口：`/dev/cu.usbserial-110`
- 串口日志波特率：`1000000`

本地仓库：

- 当前工作应用仓库：
  `/Users/wq/huangshan-pi-workspace/huangshan-pi-sf32-dev`
- SiFli SDK：
  `/Users/wq/huangshan-pi-workspace/sifli-sdk`
- 立创官方例程仓库：
  `/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example`

SDK 来源：

- 官方 SDK 仓库：`https://gitee.com/SiFli/sifli-sdk`
- 本地使用分支：`release/v2.4`

本地 SDK 中与屏幕相关的重要补丁：

- 文件：
  `/Users/wq/huangshan-pi-workspace/sifli-sdk/customer/peripherals/co5300/co5300.c`
- 当前本地 SDK 接受 CO5300 ID：`0x331100`、`0x1fff`、`0x3fff`
- 当前本地 SDK 对已验证屏幕路径使用 `HAL_LCDC_SYNC_DISABLE`

## 3. 代码来源边界

### 3.1 当前仓库里的官方 watch 例程代码

当前应用仓库里，下列代码来自立创官方 `lvgl/watch` 示例体系：

- `src/gui_apps/LC_Hello_World/`
- `src/gui_apps/rotation3d/`
- `src/gui_apps/clock/`
- `src/gui_apps/mem/`
- `src/gui_apps/utils/`
- `src/gui_apps/watch_demo.c`

其中 `src/gui_apps/mem/` 不是一个独立页面应用，而是官方图形示例和转场动画使用的缓存 / 动画内存辅助模块。

### 3.2 我们自己开发或改造的代码

当前仓库中，下列内容属于我们自己新增或改造的部分：

- `src/gui_apps/Board_Diagnostics/`
- `src/gui_apps/Codex_Test/`
- `src/gui_apps/main/app_mainmenu.c` 中当前的 `Huangshan Home` 首页

这些内容应该和官方例程分开理解。官方例程用于学习 API 和框架模式，我们自己的代码用于后续产品化和板级辅助验证。

### 3.3 官方独立例程项目

立创官方例程仓库中还有一批独立项目，每个项目都有自己的 `project/` 目录，并会生成独立固件：

- `adc/project`
- `gpio/project`
- `uart/project`
- `I2C/charger/project`
- `RT-Device/sensor/project`
- `ws2812/project`
- `lvgl/lvgl_v8_demos/project`
- `lvgl/lvgl_v9_demos/project`

这些不是 `Huangshan Home` 里的应用页面。刷入其中任意一个独立例程，都会替换当前板子上的固件。

## 4. 使用过的命令

当前应用仓库构建：

```bash
./scripts/build.sh
```

当前应用仓库刷机：

```bash
./scripts/flash.sh /dev/cu.usbserial-110
```

当前应用仓库串口监视：

```bash
SECONDS_TO_CAPTURE=10 ./scripts/monitor.sh /dev/cu.usbserial-110
```

官方独立例程构建：

```bash
source /Users/wq/huangshan-pi-workspace/sifli-sdk/export.sh
scons --board=sf32lb52-lchspi-ulp -j8
```

官方独立例程刷机：

```bash
source /Users/wq/huangshan-pi-workspace/sifli-sdk/export.sh
./uart_download.sh /dev/cu.usbserial-110
```

当前 watch app 内置官方应用启动测试命令：

```text
app_run hello_world
app_run Main
app_run rotation3d
app_run Main
app_run clock
app_run Main
```

## 5. 公共板级启动证据

多个成功启动的 GUI 固件中，都出现过以下串口证据：

```text
SFBL
Serial:c2,Chip:4,Package:3,Rev:3
Found lcd co5300 id:331100h
touch screen found driver ..., ft6146
[littlevgl2rtt] Welcome to the littlevgl2rtt lib.
display on
```

在本轮官方独立例程测试结束后，板子已经刷回我们自己的当前应用固件，串口确认：

```text
[Huangshan_Home] start
```

这说明板子最终状态已经恢复到我们自己的 `Huangshan Home` 固件，而不是停留在某个官方独立例程固件里。

## 6. 当前仓库内置官方应用测试结果

这批测试是在我们当前应用固件中进行的。测试前已经完成构建和刷机。

| 应用 ID | 源码目录 | 结果 | 证据摘要 |
| --- | --- | --- | --- |
| `hello_world` | `src/gui_apps/LC_Hello_World/` | 通过 | 能加载、启动、恢复，并返回 `Main` |
| `rotation3d` | `src/gui_apps/rotation3d/` | 通过 | 能加载、启动、恢复，并返回 `Main` |
| `clock` | `src/gui_apps/clock/` | 通过 | 表盘能初始化、恢复、暂停和释放 |

### 6.1 `hello_world`

串口证据：

```text
send msg[GUI_APP_MSG_RUN_APP] [hello_world]
app[hello_world] do LOAD
finding hello_world in builtin apps...
page[hello_world][root] do ONSTART
page[hello_world][root] do ONRESUME
page[hello_world][root] do ONSTOP
```

结论：

- 最小 GUI 应用注册和生命周期路径是可用的。
- 后续新建内置 GUI 应用时，可以优先参考这个例程的结构。

### 6.2 `rotation3d`

串口证据：

```text
send msg[GUI_APP_MSG_RUN_APP] [rotation3d]
app[rotation3d] do LOAD
finding rotation3d in builtin apps...
page[rotation3d][root] do ONSTART
last_dir=0 cur=1 -400,-560,303
page[rotation3d][root] do ONRESUME
app[rotation3d] do DESTROY
```

结论：

- 3D 旋转示例可以通过应用框架正常启动和销毁。
- 它是当前最适合学习 LVGL timer、动画启动和动画清理的官方例程。

### 6.3 `clock`

串口证据：

```text
send msg[GUI_APP_MSG_RUN_APP] [clock]
app[clock] do LOAD
finding clock in builtin apps...
page[clock][root] do ONSTART
service_reset_time:  122:6:1 - 9:0:22 - 5
STATE_PAUSED: clock[simple] init
STATE_ACTIVE: clock[rotate_b] init
STATE_ACTIVE: clock[rotate_b] resume
STATE_DEINIT: clock[rotate_b] pause
STATE_DEINIT: clock[rotate_b] deinit
STATE_DEINIT: clock[simple] deinit
```

结论：

- 时钟示例可以正常启动和退出。
- 日志中的 `122` 是 C 标准库 `struct tm.tm_year` 格式，不是直接展示给用户的年份。
- `clock` 是当前最适合学习多视图 / 多模式状态管理的官方例程。

## 7. 官方独立例程构建矩阵

下列独立例程均使用当前 SDK 和 `sf32lb52-lchspi-ulp` 目标完成构建，并生成了 `main.bin`。

| 项目 | 固件大小 | 构建结果 | 板上运行结果 |
| --- | ---: | --- | --- |
| `adc/project` | 307004 bytes | 通过 | 通过 |
| `gpio/project` | 251608 bytes | 通过 | 通过 |
| `uart/project` | 304444 bytes | 通过 | 未完整测试 |
| `I2C/charger/project` | 311092 bytes | 通过 | 未刷机 |
| `RT-Device/sensor/project` | 325304 bytes | 通过 | 通过 |
| `ws2812/project` | 305960 bytes | 通过 | 未完整测试 |
| `lvgl/lvgl_v8_demos/project` | 761400 bytes | 通过 | 串口初始化通过 |
| `lvgl/lvgl_v9_demos/project` | 882836 bytes | 通过 | 串口 / sysmon 通过 |

## 8. 官方独立例程板上测试结果

### 8.1 ADC 例程

板上结果：通过。

串口证据：

```text
Start adc demo!
ch[7]voltage=43504;
VBAT read value: 43504
adc control origin data 825, Voltage 109
ADC read value: 109
ADC example end
```

同一轮测试中还观察到：

- VBAT 原始值大约在 `43104` 到 `43674` 之间；
- PA34 ADC 电压值后续大约在 `777` 到 `893` mV 之间。

结论：

- ADC 固件可以启动并周期读取 ADC。
- ADC1 在 RT-Thread 设备框架中暴露为 `bat1`。
- 该例程使用 channel 7 读取 VBAT，使用 channel 6 读取 PA34。
- 具体数值受供电、电池状态、按键状态影响，不应作为固定期望值。

### 8.2 GPIO 例程

板上结果：通过。

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

结论：

- PA20 输出高低电平并读回成功。
- KEY2 / PA43 中断回调成功触发。
- 对该芯片系列来说，GPIO 编号可以直接对应 PA 编号。

### 8.3 LVGL v8 Demos

板上结果：串口初始化通过；视觉效果仍需肉眼确认。

串口证据：

```text
Found lcd co5300 id:331100h
touch screen found driver ..., ft6146
[littlevgl2rtt] Welcome to the littlevgl2rtt lib.
display on
```

结论：

- LVGL v8 demo 固件可以初始化 LCD、触摸、framebuffer 和 LVGL。
- 抓取窗口内没有观察到崩溃或重启。
- 这个项目是独立 LVGL 主循环，不使用 watch app framework。

### 8.4 LVGL v9 Demos

板上结果：串口 / sysmon 通过；视觉效果仍需肉眼确认。

串口证据：

```text
Found lcd co5300 id:331100h
touch screen found driver ..., ft6146
[littlevgl2rtt] Welcome to the littlevgl2rtt lib.
display on
sysmon: 62 FPS ...
sysmon: 39 FPS ...
```

结论：

- LVGL v9 demo 固件可以初始化 LCD、触摸、framebuffer 和 LVGL。
- 该例程持续输出 sysmon 性能信息。
- 本轮抓取到的 FPS 大约在 `39` 到 `62` 之间，取决于当前 demo 场景。

### 8.5 RT-Device Sensor 例程

板上结果：通过。

串口证据：

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
light: 12 lux
mag, x: -389, y: 138, z: -740
acce, x: 0, y: 20, z: -1009
gyro, x: -1890, y: -4200, z: 3150
lsm6d step, step: 0
```

结论：

- I2C3 总线在 PA39 / PA40 上可以正常发现并驱动板载传感器。
- LTR303 光照传感器初始化成功，并持续输出 `light` 数据。
- MMC56X3 磁力传感器初始化成功，并持续输出 `mag` 三轴数据。
- LSM6DSL 加速度计和陀螺仪初始化成功，并持续输出 `acce` / `gyro` 数据。
- 计步器接口可以读取，当前静置测试中 `step` 保持为 `0`，后续可通过移动板子进一步验证动态变化。

## 9. 未完整板测的官方例程

### 9.1 UART 例程

构建结果：通过。

未完整测试原因：

- 该例程使用 UART2，不是板子的调试串口。
- 完整测试需要额外 USB-TTL 模块。

README 中的硬件映射：

- UART2 TX：PA19
- UART2 RX：PA18
- 需要和 USB-TTL 共地。

完整测试时应验证：

- 调试串口能看到 UART2 初始化和收包日志；
- 外接 UART2 串口能收到 `uart2 demo`；
- 从外接串口发送数据后，调试串口能打印收到的内容。

### 9.2 I2C Charger 例程

构建结果：通过。

未刷机原因：

- 该例程通过 `i2c2` 访问 AW32001 充电芯片；
- 会扫描 I2C、读取芯片 ID，并循环写入充电电流寄存器；
- 这属于真实硬件副作用，应该在明确需要时再运行。

### 9.3 WS2812 例程

构建结果：通过。

未完整测试原因：

- 需要外接 WS2812 灯珠才能验证实际效果；
- 串口只能证明程序进入颜色循环，不能证明时序和接线正确。

## 10. 开发经验提炼

### 10.1 内置应用注册方式

官方内置 GUI 应用基本使用以下模式：

```c
#define APP_ID "example_id"

static int app_main(intent_t i)
{
    gui_app_regist_msg_handler(APP_ID, msg_handler);
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(name), LV_EXT_IMG_GET(icon), APP_ID, app_main);
```

真正用于路由的是 `APP_ID`，例如：

```c
gui_app_run("example_id");
```

目录名和显示名不是最关键的，应用 ID 才是应用框架查找和启动应用的关键。

### 10.2 应用生命周期规则

应用框架会发送这些生命周期消息：

- `GUI_APP_MSG_ONSTART`
- `GUI_APP_MSG_ONRESUME`
- `GUI_APP_MSG_ONPAUSE`
- `GUI_APP_MSG_ONSTOP`

实践规则：

- 在 `ONSTART` 中创建 LVGL 根对象和基础界面；
- 在 `ONRESUME` 中启动定时器、动画或活跃状态；
- 在 `ONPAUSE` 中停止定时器和临时活动；
- 在 `ONSTOP` 中释放持久分配和删除对象。

当前最值得参考的官方例程：

- `hello_world`：最小应用结构；
- `rotation3d`：timer 和动画清理；
- `clock`：多状态 / 多视图管理。

### 10.3 板级硬件边界

应用代码不要随意配置板级 GPIO，除非已经确认该引脚不会影响屏幕、电源、充电或复位链路。

这个规则来自我们自己的 Diagnostics 黑屏问题：Diagnostics 里控制 `GPIO26/LED1` 后，进入页面会黑屏。移除该控制后，页面恢复正常。

后续建议：

- 用 `Board_Diagnostics` 做板级探测；
- 把板级事实写进文档或 BSP 层；
- 产品应用不要重复初始化 LCD、触摸、背光、电源相关逻辑。

### 10.4 两类 LVGL 项目

本轮测试材料里有两类 LVGL 写法：

第一类是 watch app framework：

- 应用注册；
- 应用 ID；
- 生命周期消息；
- 首页 / launcher 跳转。

第二类是独立 LVGL demo：

- `littlevgl2rtt_init("lcd")`；
- `lv_demo_main()`；
- 直接运行 LVGL task loop。

我们后续做正式应用时，默认应该使用第一类，也就是当前 `Huangshan Home` 所在的应用框架。只有明确要做独立固件 demo 时，才使用第二类。

## 11. 当前板子状态

官方独立例程测试结束后，板子已经刷回当前应用仓库固件。

恢复后的串口证据：

```text
Found lcd co5300 id:331100h
touch screen found driver ..., ft6146
display on
[Huangshan_Home] start
```

生成本报告前，相关测试笔记提交为：

```text
a640612 docs: record official example test results
```

## 12. 建议的下一步测试

1. 对已通过的 `RT-Device/sensor` 做动态验证：遮挡 / 照光、旋转板子、移动板子，观察 `light`、`mag`、`acce`、`gyro`、`step` 是否随动作变化。
2. 接 USB-TTL 后完整测试 UART2 收发。
3. 接 WS2812 灯珠后完整测试 LED 时序和颜色循环。
4. 明确充电电流修改是否安全后，再测试 I2C charger 例程。
5. 如果后续持续测官方例程，可以增加一份稳定的测试矩阵文档。

## 13. 总结

本轮测试后，我们对黄山派应用开发有了更具体的认识：

- 当前板子可以通过本地 patched CO5300 路径稳定点亮屏幕；
- 触摸和显示在已测试固件中可以正常初始化；
- 官方 watch 内置应用可以通过 app ID 启动并返回 `Main`；
- ADC、GPIO 和 Sensor 官方独立例程已在板上实测通过；
- LVGL v8 / v9 官方 demo 可以初始化显示和触摸；
- UART、WS2812、I2C charger 等例程需要根据外设和硬件副作用单独安排测试；
- 后续 AI 可以基于这份报告，明确区分官方例程、我们自己的应用、构建通过、板上通过和部分测试。

这份报告可以作为后续黄山派应用开发、调试和 AI 协作的基础参考。
