# SiFli 资料索引

这份文档集中记录黄山派 SF32 开发会用到的 SiFli 官方资料、SDK 文档、
立创例程和本仓库资料。外部文档只记录链接，不把 PDF 或网页全文复制进
仓库。

## 官方入口

- SiFli 官网：https://www.sifli.com/
- SF32LB52x 产品页：https://www.sifli.com/zh-hans/sf32lb52x
- SiFli Wiki：https://wiki.sifli.com/
- SiFli SDK 编程指南总入口：https://docs.sifli.com/projects/sdk/latest/sf32lb52x/index.html
- SiFli SDK Gitee：https://gitee.com/SiFli/sifli-sdk

## 芯片和硬件

- SF32LB52x 产品页：https://www.sifli.com/zh-hans/sf32lb52x
- SF32LB52x 芯片技术规格书：
  https://downloads.sifli.com/user%20manual/DS5201-SF32LB52x-%E8%8A%AF%E7%89%87%E6%8A%80%E6%9C%AF%E8%A7%84%E6%A0%BC%E4%B9%A6%20V2p5.pdf
- SF32LB52x 芯片简介：
  https://downloads.sifli.com/user%20manual/PB5201-SF32LB52x-%E4%BA%A7%E5%93%81%E7%AE%80%E4%BB%8B.pdf
- SF32LB52-DevKit-LCD：
  https://www.sifli.com/SF32LB52-DevKit-LCD

黄山派不是官方 DevKit-LCD，但它同样基于 SF32LB52x。芯片规格、SDK、
LCD/Touch/BLE/低功耗资料仍然适用；板级跳线、屏幕、传感器和接口以立创
黄山派资料为准。

## SDK 安装和构建

- 快速入门：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/quickstart/index.html
- macOS / Linux 安装：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/quickstart/install/script/unix.html
- 编译下载：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/quickstart/build.html
- 软件架构：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/quickstart/arch.html
- 配置与编译：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_development/build_and_configuration.html
- SCons 和 Kconfig：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_note/scons.html

本仓库已经封装了常用命令：

```bash
./scripts/build.sh
./scripts/flash.sh /dev/cu.usbserial-110
./scripts/monitor.sh /dev/cu.usbserial-110
```

## 应用开发

- 创建应用程序：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_development/create_application.html
- 创建板子：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_development/create_board.html
- 芯片外设驱动：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_development/drivers.html
- 应用程序启动流程：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_development/startup_flow.html
- 调试：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_development/debugging.html
- 调试和日志：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_note/debug_logging.html

## 显示、触摸和图形

- LCDC HAL：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/hal/lcdc.html
- LCD 设备驱动：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/drivers/lcd.html
- Touch Screen 设备驱动：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/drivers/touch.html
- 图形应用框架：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/middleware/gui_app_framework.html
- EZIP 图片转换工具：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_note/ezip_tool_usage.html

本项目当前最重要的显示链路是：

```text
LVGL -> littlevgl2rtt -> RT-Thread LCD device -> CO5300 driver -> LCDC/QADSPI
```

## 蓝牙、低功耗、存储和传感器

- 低功耗开发指南：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_note/low_power.html
- 经典蓝牙服务：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/middleware/bt_services/index.html
- 低功耗蓝牙服务：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/middleware/ble_services/index.html
- 分区表：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/middleware/partition_table.html
- Flash 使用指南：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_note/flash_usage.html
- Sensor 添加指南：
  https://docs.sifli.com/projects/sdk/latest/sf32lb52x/app_note/sensor.html

## 立创黄山派资料

- 立创黄山派例程仓库：
  https://github.com/OpenSiFli/lckfb-hspi-ulp_example.git
- 本地路径：
  `/Users/wq/huangshan-pi-workspace/lckfb-hspi-ulp_example`

当前仓库从立创例程的 `lvgl/watch` 路径开始，是因为这条路径已经验证了
CO5300 AMOLED、FT6146 触摸、LVGL、资源系统、启动器和烧录流程。

## 本仓库内部资料

- `README.md`：项目定位、构建、烧录、当前 Demo
- `docs/upstream.md`：上游仓库和依赖边界
- `docs/board-bringup.md`：屏幕点亮、触摸、CO5300 补丁和日志记录
- `docs/board-app-separation.md`：板子与应用分离的开发模型，以及和
  ESP-IDF 的对比
- `docs/sifli-sdk-map.md`：本机 SDK 目录和黄山派相关源码位置
- `docs/sifli-learning-path.md`：后续学习和开发路线
