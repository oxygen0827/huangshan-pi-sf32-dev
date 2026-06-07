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
- [板子与应用分离：SiFli SDK 的开发模型](docs/board-app-separation.md)
- [上游仓库记录](docs/upstream.md)
- [开发板 bring-up 记录](docs/board-bringup.md)

## 构建

在本仓库根目录执行：

```bash
./scripts/build.sh
```

等价的手动命令：

```bash
cd project
source ../../sifli-sdk/export.sh
scons --board=sf32lb52-lchspi-ulp -j8
```

## 烧录

连接开发板 USB 后执行：

```bash
./scripts/flash.sh /dev/cu.usbserial-110
```

如果不传串口参数，脚本默认使用：

```text
/dev/cu.usbserial-110
```

## 串口监视和复位

```bash
./scripts/monitor.sh /dev/cu.usbserial-110
```

该脚本会通过 RTS 复位开发板，然后以 `1000000` 波特率抓取启动日志。

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
