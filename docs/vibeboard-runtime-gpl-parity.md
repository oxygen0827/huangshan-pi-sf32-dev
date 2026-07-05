# 对标 vibeboard-runtime-gpl 的黄山派 Runtime 进度

更新时间：2026-07-05

本文对标本机目录 `/Users/hushaohong/vibe-coding/vibeboard-runtime-gpl`，整理黄山派 Runtime 目前已经实现的功能、待开发/待优化能力，以及相对 ESP32-S3 GPL Runtime 的取舍。

## 参照对象

`vibeboard-runtime-gpl` 是面向 ESP32-S3 小屏设备的成熟 Runtime/App 平台。它的核心形态是：

```text
一次烧录 Runtime 固件
  -> App 放在 /sdcard/apps/<app_id>
  -> 板子通过 WiFi 入网并提供 HTTP 8080 管理接口
  -> 浏览器/Node 工具通过 /status /apps /install /launch /stop 管理 App
  -> 板上 Launcher 负责本地启动和生命周期控制
```

黄山派项目迁移的是 Runtime/App 分离、包安装、Launcher/App Manager、能力握手和工具链这套思想；没有照搬板载 WiFi/HTTP 管理模式。黄山派默认产品方向是：

```text
一次烧录 Runtime 固件
  -> App 放在 /sdcard/apps/<app_id>
  -> 板子通过 BLE GATT 或 USB 串口被手机/电脑管理
  -> 本地 Web/桌面/iOS 统一走 RuntimeTransport 的 serial/BLE adapter
  -> 天气/AI/云端数据由手机或电脑联网后注入到板子
```

## 已实现功能

| 模块 | 黄山派当前状态 | 对标 GPL 版 |
| --- | --- | --- |
| 板级 bring-up | CO5300 AMOLED、FT6146 触摸、LVGL/watch 基座、ADC/VBAT、GPIO/KEY、UART2、AW32001 charger、WS2812/RGB 已验证 | GPL 版对应 ESP32-S3 boot、ST7789 LCD、touch、PSRAM、SD 等 bring-up |
| 传感器容错 | LSM6DSL 可读；LTR303/MMC56X3 未 ACK 时按缺席降级，不再断言崩溃 | GPL 版有 IMU/传感器能力，但硬件组合不同 |
| Runtime/App 分离 | Runtime 固件一次烧录，App 包放 `/sdcard/apps/<app_id>`，支持免重刷安装/启动 | 与 GPL 版核心架构一致 |
| App 包格式 | 支持 `manifest.json` / `app.info` / `main.lua` / assets；打包器校验 ID、路径、manifest、Lua subset 和能力白名单 | GPL 版已有更成熟的 app.info/Lua/assets 生态和 app-validator |
| App Manager / Launcher | 支持状态、列表、启动、停止、删除已停止 App、分页读取；板端 App Manager 已有触摸分页、刷新、二次确认删除 | GPL 版 Launcher 体验和生命周期控制更成熟 |
| 串口安装 | `vb_runtime_install_begin/file/end/abort` staging 链路稳定，支持冷启动恢复和失败清理 | GPL 版主要通过 HTTP staged upload/commit |
| BLE 安装 | BLE GATT 分块安装已跑通；Mac 与 iOS 共用同一命令族 | GPL 版没有把 BLE 作为主安装路径 |
| RuntimeTransport | 已抽象 `SyncRuntimeTransport` / `AsyncRuntimeTransport`，Web bridge、桌面语音桥、iOS DemoModel 上层统一走 serial/BLE adapter | GPL 版工具主要以 HTTP board URL 为中心 |
| 长 JSON 读取 | 支持 `json_read <kind> <offset> <max_bytes>` 回补；App 列表用 `apps_page` 小页聚合 | GPL 版通过 HTTP 直接返回较完整 JSON |
| 能力握手 | `vb_runtime_capabilities` / BLE `capabilities` 可供 Mac/iPhone/AI 端解析 | 与 GPL 版 capability 文档和 validator 思路一致，但能力集合不同 |
| Sensors API | `vb_runtime_sensors` / BLE `sensors` 返回传感器 JSON，串口/BLE 自动回归覆盖 | GPL 版传感器能力更偏 ESP32-S3/外设生态 |
| Power API | VBAT + AW32001 charger 只读 JSON，`power_stage` 示例和 Lua helper 已接入 | GPL 版没有同样的黄山派电源管理硬件 |
| Display API | 返回 CO5300 尺寸、状态、亮度；支持 0-100 亮度设置，串口/BLE/iOS 同 API | GPL 版有 `sys.getbrightness/setbrightness` 等 Lua 能力 |
| RGB API | `vb_runtime_rgb` / BLE `rgb` / `vibe_rgb(...)` 支持设色、读回、恢复 | GPL 版更偏 LVGL/UI，不同板载 RGB 能力 |
| Touch API | 返回 active/count/event/gesture/delta/duration/坐标，`touch_stage` 示例已跑通 | GPL 版已有 key/touch/gamepad 更完整输入层 |
| GPIO API | 当前开放 KEY1/KEY2 只读白名单，`gpio_keys_stage` 示例已接入 | GPL 版 key/gamepad/input 注入更成熟 |
| Flow 信息流 | `flow_clear/send/status` 已稳定，最近一条可持久化，App 可只读展示手机/电脑注入的文本 | GPL 版更多依赖板载 HTTP/网络和桌面 bridge |
| Voice 桥 | 串口/BLE 可录 16 kHz PCM、WAV 落盘、文本回复回写，App 可受控触发录音/清空 | GPL 版有 Voice AI bridge、I2S/audio 生态更完整 |
| 示例 App | 已有 `clock_test`、`status_test`、`sensor_stage`、`power_stage`、`display_stage`、`touch_stage`、`gpio_keys_stage`、`rgb_test`、`flow_stage`、`voice_stage`、`weather_pet`、`auto_snake` 等 | GPL 版已有大量用户向 App：2048、weather、voice_ai、NES、photos、plane、matrix 等 |
| iOS | `VibeBoardBLE` Swift package、DemoModel、RuntimePackage 校验、BLE 安装/状态读取、长 JSON fallback 已接入并测试通过 | GPL 版主要是桌面/HTTP 工具，黄山派 iOS 方向更靠前 |
| 本地 Web/桌面工具 | App Store server 通过 RuntimeTransport 管理 status/apps/capabilities/install/launch/stop/delete；语音桥串口/BLE 共用 common helper | GPL 版 Device Web UI 更完整，但依赖板载 HTTP |
| 自动回归 | `runtime_deep_check.py`、`runtime_full_reliability.py`、串口/BLE reliability、architecture audit、Swift tests、固件 build 已形成门禁 | GPL 版有 npm smoke 工具和 board HTTP smoke，覆盖面更偏 ESP32 runtime 生态 |

## 待开发 / 待优化功能

| 方向 | 当前缺口 | 推荐下一步 |
| --- | --- | --- |
| App Manager 产品化 | 目前能分页、启动、刷新、删除，但 UI 还偏工程态 | 增加图标、滚动、失败详情弹窗、安装进度、恢复提示、最近使用 |
| App 生态 | 当前多为 stage/regression app，用户向应用不足 | 先打磨天气、语音助手、传感器仪表、电源面板、RGB/触摸工具、小游戏 |
| Lua/LVGL 能力 | 目前是受控 manifest + Lua subset，不是完整 Lua VM/LVGL binding | 按真实 App 需求逐步增加高价值 binding，保留能力白名单和崩溃隔离 |
| UI 组件体系 | `Huangshan_UI_Lab` 已起步，但尚未成为 Runtime App 标准组件库 | 固化列表、卡片、状态条、按钮、图标、错误态、加载态等组件规范 |
| 资源/图片能力 | `weather_pet` 已带资源，但图片/字体/动画能力还没形成完整 App API | 明确图片格式、尺寸、缓存策略、字体策略和打包约束 |
| 手机 App 闭环 | iOS package 与 Demo 已通，仍偏验证工具 | 做成可给开发者/用户用的安装、管理、日志、升级、故障提示界面 |
| 桌面管理工具 | 本地 Web bridge 已可用，但体验还不如 GPL 版 Device Web UI | 做传输选择、设备连接状态、App 市场、安装进度、日志下载、错误解释 |
| 语音产品化 | 已有录音/回写/证据链，App 不能直接读 PCM，播放链路不足 | 增加播放/回放状态、长录音稳定性、权限提示、端侧唤醒或按钮流程 |
| 音频输出 / I2S | 录音链路已通，播放和扬声器/codec 产品路径未完整验证 | 做 audio out API、loopback/播放 smoke、音量和设备状态 JSON |
| Native module / NES | 尚未迁移 GPL 版 NES/native ABI/gamepad host | 等 Runtime API 稳定后评估 NES/native ABI、内存和显示接管风险 |
| Gamepad | 当前只有触摸/GPIO/按键状态，没有 BLE/Xbox/手柄输入层 | 先定义受控 gamepad JSON + App helper，再考虑真实手柄发现/配对 |
| Camera | 黄山派当前没有接入摄像头 Runtime | 仅在硬件确实需要时新增，避免照搬 ESP32 GC2145 路线 |
| 安全模型 | 包安装还没有签名、权限确认、BLE 配对安全、用户授权 | 增加 package signature、manifest permissions、pairing/bonding、危险能力确认 |
| 安装恢复 | 已有 staging/abort/冷恢复，仍需断点续传和多轮压力 | 增加 chunk hash、resume、事务日志、多 App 并发保护和掉电矩阵 |
| 量产测试 | 当前是开发板验证，不是工厂测试 | 增加工厂自检、硬件版本识别、日志导出、测试报告和一键恢复 |
| 发布/升级 | 缺正式固件/资源 OTA、回滚、版本兼容矩阵 | 建立 release gate、固件版本、Runtime capability version、App 最低版本策略 |
| 商业化文档 | README/开发文档较全，用户手册/售后流程还缺 | 写 C 端快速上手、恢复模式、隐私说明、故障码和支持流程 |

## 相对 GPL 版的主要差异

| 项目 | GPL ESP32-S3 Runtime | 黄山派 Runtime |
| --- | --- | --- |
| 主传输 | 板子 WiFi 入网，板载 HTTP 8080 | BLE GATT / USB 串口，手机或电脑作为管理端 |
| 网络策略 | 板子直接 WiFi/HTTP，App 可用网络能力 | 默认拒绝 `wifi/http/network/ntp/board_ip`，云端数据由 bridge 注入 |
| App API | 更完整 Lua/LVGL binding，native/gamepad/camera/I2S 等高阶能力多 | 受控 manifest + Lua subset，优先稳定、安全和可验证 |
| 设备 UI | Launcher 和 Device Web UI 更成熟 | App Manager 已可用，但仍需产品化打磨 |
| 硬件适配 | ESP32-S3 + ST7789/PSRAM/SD/WiFi | SF32LB52 + CO5300/FT6146/AW32001/LSM6DSL/RGB/电源状态 |
| 手机方向 | 主要不是手机 BLE 优先 | iOS BLE package 和 Demo 已对齐核心协议 |
| 风险取舍 | 功能多，网络和 native 能力强 | 能力更窄，但安全边界、配网体验和硬件降级更适合 C 端产品化 |

## 黄山派当前优势

- 不要求用户给板子配 WiFi；手机/电脑联网后通过 BLE/串口注入数据，更接近 C 端配网体验。
- 对黄山派真实硬件做了探测与降级，I2C 未 ACK 不会直接变成 Runtime 崩溃。
- Web、桌面、iOS 上层已经统一到 `RuntimeTransport`，后续新增能力可以优先加在协议和 adapter 层。
- 受控 manifest 和 Lua subset 更容易做权限、签名、AI 生成 App 校验和崩溃隔离。
- 自动回归覆盖了串口/BLE真实硬件 core gate，不只是“能传文件”。

## 近期推荐路线

1. 把 App Manager 做到可日常使用：图标、滚动、错误详情、安装进度、恢复提示。
2. 打磨 3-5 个 C 端可展示 App：天气宠物、语音助手、传感器仪表、电源面板、小游戏。
3. 完善手机/桌面 bridge：设备连接、App 安装、日志、故障解释、云数据注入。
4. 增加包签名、权限模型、BLE 配对安全和版本兼容策略。
5. 做长时稳定性和掉电恢复矩阵，再评估 NES/native/gamepad/audio out 等高阶能力。
