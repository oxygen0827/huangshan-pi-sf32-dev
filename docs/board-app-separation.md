# 板子与应用分离：SiFli SDK 的开发模型

这篇文档解释 SiFli SDK 里“板子”和“应用”分离的思路，并和 ESP-IDF
常见项目模型做对比。重点不是概念好不好听，而是它对黄山派后续开发有什么
实际价值。

## 一句话解释

板子与应用分离，就是把“这块硬件怎么工作”和“我要做什么产品功能”拆开。

```text
板子 board = 芯片、屏幕、触摸、Flash、PSRAM、引脚、分区、电源、启动
应用 app  = UI、业务逻辑、传感器展示、蓝牙服务、音频、文件系统、交互
```

这样做以后，同一个应用可以尝试编译到不同板子；同一块板子也可以跑不同
应用。硬件事实不会散落在每个应用里。

这和 Linux 里的分层思想相似，但不是同一种隔离强度。Linux 是内核、驱动、
设备树和用户态应用分离；SiFli 仍然是 MCU/RTOS，最终会链接成固件镜像。
它更准确的名字是：

```text
BSP / HAL / RT-Thread driver / middleware / application 分层
```

## 为什么这个思路重要

如果没有这层分离，做一个带屏幕的 MCU 项目很容易变成这样：

```text
应用 A 里写一遍屏幕初始化
应用 B 里再写一遍触摸初始化
应用 C 里又复制一份 PSRAM 和 Flash 配置
每个 Demo 都有一套似是而非的板级假设
```

最后的问题是：应用越多，板级知识越分散。屏幕黑屏、触摸不准、Flash 分区
变了、低功耗唤醒失败时，很难判断问题属于硬件适配还是应用逻辑。

板子与应用分离的目标是把这些事实固定下来：

- 这块板子用什么芯片和封装
- 外部 Flash、PSRAM 多大，接在哪条总线上
- LCD 和触摸分别用什么驱动
- 哪些 GPIO 是按键、LED、功放、电源控制
- 分区表怎么放
- HCPU / LCPU 如何构建
- 哪些驱动补丁是这块板子的必要条件

应用层只消费这些能力，不重复定义这些事实。

## SiFli SDK 里的分层

SiFli SDK 顶层目录本身就体现了这种分层：

```text
sifli-sdk/
  customer/
    boards/          板级配置
    peripherals/     板级外设驱动
  drivers/
    cmsis/           芯片寄存器、启动文件、链接脚本
    hal/             HAL 驱动实现
    Include/         HAL 头文件
  rtos/
    rtthread/        RT-Thread 和设备驱动适配
    freertos/        FreeRTOS
    os_adaptor/      OS 抽象层
  middleware/        SiFli 自研中间件
  external/          第三方组件，如 LVGL、FlashDB、mbedtls
  example/           官方例程
  tools/             构建、烧录、资源和调试工具
```

这不是一个“所有代码都放在 app 目录里”的 SDK。它把板级支持、芯片 HAL、
RTOS 驱动、中间件、第三方库和应用例程拆成了不同层。

对黄山派来说，最关键的板级目录是：

```text
sifli-sdk/customer/boards/sf32lb52-lchspi-ulp/
  hcpu/
    board.conf
    custom_mem_map.h
    Kconfig
    Kconfig.board
    rtconfig.py
  lcpu/
    board.conf
    custom_mem_map.h
    Kconfig
    Kconfig.board
    rtconfig.py
  ptab.json
  SConscript
```

这里不是应用代码，而是“这块板子是什么”的定义。

## 黄山派板级层负责什么

在黄山派的 `hcpu/board.conf` 里，可以看到一批典型板级能力：

```text
CONFIG_BSP_USING_GPIO=y
CONFIG_BSP_USING_UART=y
CONFIG_BSP_USING_SPI=y
CONFIG_BSP_USING_I2C=y
CONFIG_BSP_ENABLE_MPI1=y
CONFIG_BSP_ENABLE_MPI2=y
CONFIG_BSP_USING_LCD=y
CONFIG_LCD_USING_TFT_CO5300=y
CONFIG_BSP_USING_TOUCHD=y
CONFIG_BSP_USING_KEY1=y
CONFIG_BSP_USING_KEY2=y
CONFIG_BSP_USING_LED1=y
CONFIG_BSP_ENABLE_AUD_PRC=y
CONFIG_BSP_ENABLE_AUD_CODEC=y
```

这些配置回答的是硬件问题：

- 这块板子启用哪些外设
- LCD 是 CO5300
- 有触摸设备
- 有按键、LED、音频
- 有 MPI/Flash/PSRAM 相关总线
- 控制台日志怎么输出

这类内容不应该写在每个应用里。应用只应该说“我要画一个界面”“我要读一个
传感器”“我要开一个 BLE 服务”。

## 应用层负责什么

当前仓库是应用工作区：

```text
huangshan-pi-sf32-dev/
  project/
    SConstruct
    SConscript
    proj.conf
    Kconfig.proj
  src/
    gui_apps/
    resource/
    app_utils/
```

构建时通过板型选择黄山派：

```bash
scons --board=sf32lb52-lchspi-ulp -j8
```

这句命令的意思不是“编译一个叫黄山派的应用”，而是：

```text
把当前应用工程，编译到 sf32lb52-lchspi-ulp 这块板子上。
```

当前应用层包括：

- `src/gui_apps/Codex_Test/`：第一个自定义测试应用
- `src/gui_apps/main/`：当前启动器
- `src/resource/strings/`：字符串资源
- `src/resource/images/`：图片资源
- `src/app_utils/`：应用辅助逻辑

这些内容可以变。今天是手表 UI 壳，明天可以是传感器仪表盘、BLE 控制器、
音频测试器或屏幕调试工具。只要底层板级事实稳定，应用可以迭代得更快。

## 构建过程里如何体现分离

本项目的 `project/SConscript` 先引入 SDK，再引入应用源码：

```text
SDK -> sifli_sdk variant
App -> ../src/SConscript
```

板级目录自己的 `SConscript` 又根据 `BSP_USING_BOARD_SF32LB52_LCHSPI_ULP`
选择黄山派板级驱动和基础脚本。

可以把整个构建理解成：

```text
选择 board
  -> 加载 board 配置、内存、外设、分区
  -> 加载 SDK HAL / RT-Thread / middleware
  -> 加载当前 app 源码
  -> 生成 bootloader、main、ftab、download script
```

这也是为什么我们的构建日志里会看到 `bootloader`、`main`、`ftab`，而不是
只有一个简单的 `app.bin`。

## 和 Zephyr 的相似点

Zephyr 常见命令是：

```bash
west build -b nrf52840dk_nrf52840 samples/blinky
```

意思是把 `samples/blinky` 这个应用，编译到 `nrf52840dk_nrf52840` 这块
板子上。

SiFli 的味道类似：

```bash
scons --board=sf32lb52-lchspi-ulp -j8
```

它也强调：

```text
应用工程是一回事
目标板定义是另一回事
构建时把两者组合起来
```

这就是我说它“类似 Zephyr 思路”的原因。

## 和 Linux 的相似点与差异

相似点：

```text
Linux:
  kernel / driver / device tree / rootfs / userspace app

SiFli:
  board BSP / HAL / RT-Thread driver / middleware / app
```

两者都希望把硬件适配从应用逻辑里拿出来。

关键差异：

- Linux 有内核态和用户态，隔离强。
- SiFli 是 MCU 固件，最终大多链接进同一个镜像或相关镜像。
- Linux 应用通常不关心驱动编译；SiFli 应用仍然会受 Kconfig、链接脚本、
  Flash 分区、PSRAM 策略影响。
- Linux 可以运行时加载很多东西；SiFli 更多是构建期组合。

所以它不是“MCU 上的小 Linux”，而是“把 Linux 式硬件/应用分层思想带进
RTOS 固件工程”。

## ESP-IDF 是怎么处理的

ESP-IDF 也有分层，只是重心不同。

ESP-IDF 官方把项目看成多个 component 的组合：

```text
ESP-IDF project
  CMakeLists.txt
  sdkconfig
  partitions.csv
  main/
  components/
  managed_components/
```

它的核心概念是：

- `project`：构建一个 app 的目录
- `sdkconfig`：这个项目的统一配置
- `components`：可复用代码模块
- `target`：芯片目标，如 `esp32`、`esp32s3`、`esp32c6`
- `IDF_PATH`：外部 ESP-IDF 框架路径

ESP-IDF 当然也把 framework 和 project 分开。`esp-idf` 不属于你的项目，
项目通过 `IDF_PATH` 引用它。这一点和 SiFli 的外部 SDK 依赖相似。

但是 ESP-IDF 的常见开发体验更偏：

```text
这个项目选择哪个芯片 target
这个项目启用哪些 sdkconfig
这个项目包含哪些 components
这个项目用哪个 partition table
```

而不是先显式定义一个完整 board，再让多个应用复用这个 board。

## ESP-IDF 的优势

ESP-IDF 的优势很明显：

- 生态成熟，资料多
- CMake/component 模型清晰
- Wi-Fi、BLE、NVS、OTA、HTTP、MQTT 等网络能力强
- Component Manager 适合拉第三方组件
- 一个项目内管理依赖、配置、分区很方便
- VS Code、PlatformIO、Arduino 生态都更成熟

如果项目核心是联网、云端、MQTT、Home Assistant、HTTP API 或大量复用
第三方库，ESP-IDF 的开发效率通常更高。

## ESP-IDF 的板级边界弱在哪里

ESP-IDF 不是没有硬件抽象，但它常见的“board”边界没有 SiFli/Zephyr 这种
项目体验那么强。

典型 ESP-IDF 项目里，硬件适配常常分散在：

```text
sdkconfig
sdkconfig.defaults
partitions.csv
main/board_xxx.c
components/<driver>/
Kconfig.projbuild
menuconfig 选项
项目 README
```

这样也能做得很好，但团队需要自己建立约束：

- 哪些 GPIO 是板级事实
- 哪些 component 是通用驱动
- 哪些配置是产品配置
- 多个板子共用一个应用时目录怎么组织
- 同一个板子跑多个应用时配置怎么继承

ESP-IDF 给了很强的 component 工具箱，但“板子是一个一等实体”这件事，在
常规项目体验里没有 SiFli/Zephyr 那么突出。

## 两者对比

| 维度 | SiFli SDK | ESP-IDF |
|---|---|---|
| 主要抽象 | board + app + SDK 分层 | project + component + target |
| 构建工具 | SCons、Kconfig、proj.conf | CMake、idf.py、sdkconfig |
| 板级目录 | `customer/boards/<board>` 很明确 | 通常由项目自己组织 |
| 应用目录 | 可独立于 board 存在 | 通常和项目配置绑定更紧 |
| 驱动层 | HAL + RT-Thread device driver | ESP-IDF driver components |
| 生态资料 | 较少，需读 SDK/例程源码 | 非常丰富 |
| 强项 | 显示、穿戴、低功耗、板级 HMI | Wi-Fi、联网、云端、通用 IoT |
| 风险 | 文档少，板级坑需要自己沉淀 | 项目配置容易和硬件事实混在一起 |

## 对黄山派项目的意义

黄山派这块板子不只是一个 MCU breakout。它有完整的产品级外设组合：

- CO5300 AMOLED
- FT6146 触摸
- OPI PSRAM
- QSPI NOR Flash
- RGB LED
- MIC、音频 PA
- IMU、地磁、环境光
- TF 卡
- 按键和电源路径

这些都属于板级事实。

我们已经遇到过一个典型例子：CO5300 屏幕黑屏。最终它不是应用 UI 写错，
而是板级 LCD 驱动和 TE sync/ID 识别的问题。

如果没有板级/应用分离，很容易误判成：

```text
是不是 LVGL 写错了？
是不是 UI 没刷新？
是不是颜色没设置？
是不是应用没启动？
```

而正确归类应该是：

```text
这是 sf32lb52-lchspi-ulp + CO5300 面板链路的 board bring-up 问题。
```

这就是分层带来的价值：问题能归位。

## 本仓库后续应该怎么用这个思想

本仓库应保持三个层次：

```text
1. 上游 SDK
   /Users/wq/huangshan-pi-workspace/sifli-sdk

2. 板级知识
   docs/board-bringup.md
   docs/sifli-sdk-map.md
   docs/sifli-resources.md
   必要的 SDK board/peripheral patch

3. 应用开发
   src/gui_apps/
   src/resource/
   project/proj.conf
```

新增功能时，先判断它属于哪一层：

- 屏幕 ID、触摸驱动、Flash 分区、PSRAM、按键 GPIO：板级层
- LVGL 页面、传感器展示、BLE 数据协议、音频播放器：应用层
- HAL bug、CO5300 驱动适配、RT-Thread device 注册：SDK/驱动层

不要把板级修复藏在某个 UI 应用里。也不要为了一个应用随意改板级配置，
除非这个改动能解释清楚并记录到文档。

## 一个推荐目录演进

当前仓库可以继续演进成：

```text
src/gui_apps/
  Codex_Test/          当前最小验证 App
  Board_Test/          板级自检 App
  Sensor_Dashboard/    传感器仪表盘
  Ble_Controller/      BLE 控制器
  Watch_Shell/         手表 UI 壳

docs/
  board-bringup.md
  board-app-separation.md
  sifli-sdk-map.md
  sifli-resources.md
  sifli-learning-path.md
```

这样每个应用可以专注自己的功能，而共同依赖的屏幕、触摸、烧录、分区和
外设知识会沉淀在文档和板级层。

## 结论

SiFli 这套模型真正有价值的地方，不是它用了 SCons，也不是它像 Zephyr。

它的价值是：对于黄山派这种带屏幕、触摸、PSRAM、蓝牙、传感器和低功耗
能力的复杂 MCU 板子，它给了我们一个更清晰的开发边界。

```text
板子先稳定
应用再增长
问题能归位
经验能复用
```

ESP-IDF 更适合网络型 IoT 和组件生态驱动的快速开发。SiFli 这种
board/app 分离模型，更适合把黄山派沉淀成一个长期可复用的图形化低功耗
终端开发基座。

## 参考资料

- SiFli SDK 编程指南：https://docs.sifli.com/projects/sdk/latest/sf32lb52x/index.html
- SiFli SDK 软件架构：https://docs.sifli.com/projects/sdk/latest/sf32lb52x/quickstart/arch.html
- SiFli SDK 创建板子：https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_development/create_board.html
- SiFli SDK 创建应用程序：https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_development/create_application.html
- SiFli SDK 配置与编译：https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_development/build_and_configuration.html
- ESP-IDF Build System：https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html
- ESP-IDF Project Configuration：https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-guides/kconfig/project-configuration-guide.html
- ESP-IDF Partition Tables：https://documentation.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html
