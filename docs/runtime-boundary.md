# 黄山派 Runtime 边界

更新时间：2026-07-11

这份文档定义黄山派 VibeBoard Runtime 的工程边界。它用来约束后续从
`vibeboard-runtime-gpl` 迁移能力时的取舍：先迁移 Runtime/App 分离、包格式、
能力握手和工具链；不要直接照搬 ESP32-S3 的板载 WiFi、HTTP、native、camera
或 gamepad 假设。

## 一句话边界

```text
Runtime 固件负责稳定运行、硬件抽象、App 生命周期和受控能力。
Host/iOS/Web 负责联网、安装管理、日志解释、AI/云端数据注入和用户界面。
Runtime App 只声明 UI/交互和需要的受控能力，不直接碰板级驱动或网络。
```

## Runtime 固件负责

- 扫描 `/sdcard/apps/<app_id>` 并维护 `.active`。
- 通过 staging 目录完成 `install_begin/file/end/abort` 事务。
- 在提交前校验包完整性：`main.lua`、`manifest.json` 或 `app.info`，以及
  manifest `files[]` 中每个文件的大小和 SHA-256。
- 暴露串口 MSH 和 BLE GATT 命令族。
- 驱动 CO5300、FT6146、AW32001、VBAT、KEY、RGB、LSM6DSL 等黄山派硬件。
- 对缺席硬件做降级，不把 I2C 未 ACK 变成 Runtime 崩溃。
- 执行 Lua 5.5 完整语言 VM 和受控 Runtime/LVGL helper；限制内存、指令数、文件范围和标准库。
- 维护能力 JSON：display、sensor、power、touch、gpio、rgb、flow、voice、audio 等。
- 通过 SiFli `audio_server` 播放 App 包内受支持的 PCM WAV，不向 App 暴露底层 I2S。

## Host/iOS/Web 负责

- 选择 serial 或 BLE transport，并串行化对板命令。
- 离线校验包：App ID、路径、manifest、Lua 文件安全边界、能力白名单。
- 安装前生成或补齐 manifest `files[]` 和 `integrity.filesDigest`。
- 展示 App 管理界面、安装进度、错误解释和日志。
- 注入板子不能自己获取的数据，例如天气、AI 回复、信息流和云端结果。
- 做桥接能力，例如语音 WAV 导出、桌面/手机网络请求、开发者调试 UI。

## Runtime App 负责

- 提供 `main.lua` 入口。
- 用 `manifest.json` 声明 Huangshan profile、组件、能力和资源。
- 可以使用函数、表、循环、闭包、模块等完整 Lua 语法，只调用白名单 host helper，
  例如 `vibe_label(...)`、`vibe_audio_play(...)`、`vibe_2048_game(...)`。
- 把复杂交互拆成 Runtime 原生 helper，再由 Lua 触发。
- 不直接声明 ESP32 原生能力，例如 `wifi`、`http`、`network`、`camera`、
  `native`、`gamepad`、`i2s`。

## 当前模块边界

| 模块 | 职责 |
| --- | --- |
| `src/gui_apps/VibeBoard_Runtime/main.c` | Runtime 主状态机、命令、UI、硬件能力和 App 执行 |
| `src/gui_apps/VibeBoard_Runtime/vb_runtime_package.c` | staging 包完整性、manifest 基础字段、`files[]` 大小和 SHA-256 校验 |
| `scripts/runtime_package.py` | 主机侧包校验、manifest integrity 生成、CLI 自测 |
| `scripts/runtime_app_plan_writer.py` | AI/开发者 JSON plan 到 Huangshan Runtime App 包的生成入口 |
| `mobile/ios/VibeBoardBLE/.../RuntimePackage.swift` | iOS 侧同等包校验和 install command 生成 |
| `scripts/runtime_transport.py` | serial/BLE transport 抽象，上层 Web/脚本共用 |
| `scripts/app_store_server.py` | 本地 Web 管理体验和桥接服务 |

## 从 GPL 项目迁移的优先级

优先迁移：

- 包格式和完整性校验。
- App packager/uploader 的校验思想。
- Device Web UI 的管理体验，但底层走 `RuntimeTransport`，不是板载 HTTP。
- app-plan-writer 生成 manifest/Lua/assets 的工具链。
- smoke test、validator 和发布 gate。

已迁移并保持受控：

- Lua 5.5 完整语言 VM；不等于完整 LVGL binding 或不受限标准库。
- 高层 PCM WAV 播放；不等于向 App 开放 I2S/codec 驱动。

暂缓迁移：

- 完整 LVGL binding、native module、NES、camera、gamepad 和直接 I2S。
- 板载 WiFi、HTTP server、NTP、board IP 直连能力。

判断标准：只有当能力可以通过 Huangshan profile、manifest 权限、离线校验和
板端降级路径说清楚时，才进入 Runtime 固件。

详细评估和重新打开条件见
[`runtime-high-risk-capabilities-evaluation.md`](runtime-high-risk-capabilities-evaluation.md)。
