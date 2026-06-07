# 黄山派 SiFli 学习路线

这份路线面向后续实际开发，不按官方文档顺序照搬。目标是尽快掌握这块
SF32LB52x 板子的系统模型、构建方式、屏幕 UI、外设和低功耗。

## 1. 先建立芯片和板子的模型

先看：

- SF32LB52x 产品页
- SF32LB52x 芯片技术规格书
- 本仓库 `README.md`
- 本仓库 `docs/board-bringup.md`

要掌握的概念：

- HCPU / LCPU 大小核架构
- 576KB 片上 SRAM、外部 OPI PSRAM、QSPI NOR Flash
- CO5300 AMOLED 走 QADSPI 显示链路
- FT6146 触摸走 I2C
- SDK 是 RT-Thread 体系，不是 ESP-IDF

## 2. 跑通构建、烧录、串口日志

先确保这三个命令稳定可用：

```bash
./scripts/build.sh
./scripts/flash.sh /dev/cu.usbserial-110
./scripts/monitor.sh /dev/cu.usbserial-110
```

能看到以下日志，说明板级基础链路正常：

```text
Found lcd co5300 id:331100h
display on
[littlevgl2rtt] Welcome to the littlevgl2rtt lib.
touch screen found driver ... ft6146
```

## 3. 学 SCons、Kconfig、proj.conf

SiFli 项目不是 ESP-IDF 的 CMake component 模型。这里要重点看：

- `project/SConstruct`
- `project/SConscript`
- `project/proj.conf`
- `project/Kconfig.proj`
- `src/SConscript`
- `src/gui_apps/SConscript`

目标：

- 知道一个 `.c` 文件如何被加入构建
- 知道一个应用目录如何被自动扫描
- 知道 board 参数如何选择 `sf32lb52-lchspi-ulp`
- 知道 `proj.conf` 和 Kconfig 如何打开功能

## 4. 学 LVGL 和当前应用壳

先从当前能跑的应用开始：

```text
src/gui_apps/Codex_Test/main.c
src/gui_apps/LC_Hello_World/main.c
src/gui_apps/main/app_mainmenu.c
src/resource/strings/zh_cn.json
src/resource/strings/en_us.json
```

目标：

- 会创建一个新 `gui_apps/<AppName>/`
- 会写 `SConscript`
- 会注册 `BUILTIN_APP_EXPORT`
- 会增加字符串资源
- 会从启动器进入应用、返回主界面

## 5. 学屏幕链路

重点看：

- `docs/board-bringup.md`
- SDK `customer/peripherals/co5300/co5300.c`
- SDK LCDC 文档
- SDK LCD 设备驱动文档

目标：

- 知道 CO5300 ID 为什么可能读到 `0x1fff` 或 `0x3fff`
- 知道 `HAL_LCDC_SYNC_DISABLE` 对当前板子的意义
- 知道黑屏、超时、花屏应该从哪些日志判断

## 6. 学触摸、传感器和外设

下一批适合做成板级测试应用：

- 触摸测试
- RGB LED 测试
- 按键测试
- IMU 测试
- 地磁测试
- 环境光测试
- TF 卡测试
- MIC / 音频输出测试

优先参考 SDK：

```text
example/rt_device/gpio
example/rt_device/i2c
example/rt_device/spi
example/rt_device/spi_tf
example/rt_device/pdm
example/rt_device/audprc
example/rt_device/rgb_led
example/rt_device/pm
```

## 7. 学蓝牙和低功耗

黄山派这颗芯片的优势不是 Wi-Fi，而是蓝牙、显示和低功耗。

先看：

- SDK 低功耗开发指南
- SDK BLE peripheral 例程
- SDK PM 例程
- SDK 电源管理中间件

优先目标：

- BLE 广播
- BLE 连接
- 抬腕/按键唤醒
- 屏幕开关
- 空闲功耗观察

## 8. 后续项目方向

适合继续做：

- 黄山派板级自检应用
- 传感器仪表盘
- BLE 控制器
- 小型触摸终端
- 离线 UI Demo
- 低功耗显示设备
- 手表 UI 原型

不建议一开始做：

- 复杂云端联网应用
- Wi-Fi 网关
- 大量依赖 Arduino 库的项目
- 没有串口日志和板级测试支撑的大型 UI

## 9. 每次新增功能的推荐流程

1. 先在 SDK 或立创例程里找到相近例程。
2. 在本仓库新增一个最小测试 App。
3. 构建并烧录。
4. 用串口日志确认硬件链路。
5. 再接入正式 UI 或业务逻辑。
6. 把关键日志和坑补充到 `docs/board-bringup.md` 或新文档。
