# SiFli SDK 本地地图

本机 SiFli SDK 路径：

```text
/Users/hushaohong/vibe-coding/huangshan-pi-workspace/sifli-sdk
```

本仓库默认从兄弟目录 `../sifli-sdk` 使用 SDK。脚本仍支持通过
`SIFLI_SDK_PATH` 指定其他 SDK 位置。

## 总体结构

```text
sifli-sdk/
  customer/       板级支持包、板级外设驱动
  drivers/        CMSIS、HAL、芯片启动文件、链接脚本
  example/        官方例程
  external/       第三方组件，如 LVGL、FlashDB、mbedtls
  middleware/     SiFli 自研中间件
  rtos/           RT-Thread、FreeRTOS、OS adapter
  tools/          构建、烧录、资源和调试工具
```

SiFli SDK 是基于 RT-Thread 定制的软件框架。开发时通常会同时接触 HAL、
RT-Thread 设备驱动、中间件和应用工程。

## 黄山派目标板

当前板型：

```text
sf32lb52-lchspi-ulp
```

SDK 里的板级目录：

```text
customer/boards/sf32lb52-lchspi-ulp/
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

HCPU 是主应用侧，负责 UI、应用逻辑和主要外设。LCPU 主要服务低功耗和
蓝牙相关任务。构建日志里能看到 HCPU、bootloader、ftab 等多工程信息。

## 当前项目构建入口

本仓库工程入口：

```text
huangshan-pi-sf32-dev/project/
  SConstruct
  SConscript
  proj.conf
  Kconfig.proj
```

构建命令：

```bash
./scripts/build.sh
```

等价于：

```bash
cd project
source ../../sifli-sdk/export.sh
scons --board=sf32lb52-lchspi-ulp -j8
```

## 显示和触摸相关源码

当前屏幕链路重点文件：

```text
customer/peripherals/co5300/co5300.c
drivers/Include/bf0_hal_lcdc.h
drivers/hal/bf0_hal_lcdc.c
rtos/rtthread/bsp/sifli/drivers/
external/lvgl_v8/
external/lvgl_v9/
```

当前已验证的 CO5300 补丁位于：

```text
customer/peripherals/co5300/co5300.c
```

关键行为：

- 接受 `0x331100`、`0x1fff`、`0x3fff` 三种读 ID
- 当前路径禁用 LCDC TE sync，使用 `HAL_LCDC_SYNC_DISABLE`

## 官方例程目录

常用例程分类：

```text
example/get-started/     hello_world、blink
example/lcd_fb/          LCD framebuffer 测试
example/lcd_stress_test/ LCD 压力测试
example/gpu/             图形/GPU 例程
example/vglite/          VGLite 图形例程
example/multimedia/lvgl/ LVGL 相关例程
example/rt_device/       RT-Thread 设备驱动例程
example/hal/             HAL 外设例程
example/ble/             BLE 例程
example/bt/              经典蓝牙例程
example/pm/              低功耗例程
example/storage/         FatFs、FlashDB 等存储例程
example/system/          finsh、ulog 等系统组件例程
example/multicore/       多核和核间通信例程
```

对黄山派后续开发，优先参考：

- `example/get-started/hello_world`
- `example/lcd_fb`
- `example/lcd_stress_test`
- `example/multimedia/lvgl`
- `example/rt_device/gpio`
- `example/rt_device/i2c`
- `example/rt_device/spi`
- `example/rt_device/pdm`
- `example/rt_device/spi_tf`
- `example/ble/peripheral`
- `example/pm`

## 立创例程对应关系

立创例程路径：

```text
/Users/hushaohong/vibe-coding/huangshan-pi-workspace/lckfb-hspi-ulp_example
```

本仓库当前复制了其中的 `lvgl/watch` 应用结构。它提供了一个已经验证可用的
LVGL 启动器、资源系统和应用注册方式。

当前自定义应用：

```text
src/gui_apps/Codex_Test/
```

注册方式：

```c
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(codex_test), LV_EXT_IMG_GET(img_LiChuang), APP_ID, app_main);
```

## 调试入口

常用命令：

```bash
./scripts/build.sh
./scripts/flash.sh /dev/cu.usbserial-13220
./scripts/monitor.sh /dev/cu.usbserial-13220
```

常看日志：

- LCD ID 是否识别为 CO5300
- 是否打印 `display on`
- 是否出现 `littlevgl2rtt`
- 是否发现 FT6146 touch
- 应用是否收到 `GUI_APP_MSG_RUN_APP`
- 是否出现 `draw_core timeout` 或 LCDC timeout
