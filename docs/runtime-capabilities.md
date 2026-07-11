# 黄山派 Runtime 能力表

更新时间：2026-07-10

黄山派 Runtime 能力以 `vb_runtime_capabilities` 的 JSON 为机器握手入口，以
manifest/Lua helper 白名单为 App 侧边界。能力命名描述的是黄山派 profile，不是
ESP32-S3 GPL Runtime 的完整能力集合。

## 已开放能力

| 能力 | 板端命令 / App helper | 说明 |
| --- | --- | --- |
| Runtime 状态 | `vb_runtime_status`、`vb_runtime_app` | active app、运行状态、错误、计数 |
| App 管理 | `vb_runtime_apps`、`apps_page`、`launch/stop/delete` | Web/iOS/桌面管理入口 |
| 安装 | `install_begin/file/end/abort` | serial/BLE 共用，提交前校验包完整性 |
| Display | `vb_runtime_display [brightness]`、`vibe_display_label`、`vibe_display_brightness` | CO5300 尺寸、状态、亮度 |
| Sensor | `vb_runtime_sensors`、`vibe_sensor_label` | LSM6DSL 已验证；LTR303/MMC56X3 缺席降级 |
| Power | `vb_runtime_power`、`vibe_power_label` | VBAT 和 AW32001 只读快照 |
| Touch | `vb_runtime_touch`、`vibe_touch_label` | 最近触摸事件、坐标、手势、计数 |
| GPIO | `vb_runtime_gpio`、`vibe_gpio_label` | KEY1/KEY2 只读白名单 |
| RGB | `vb_runtime_rgb`、`vibe_rgb` | 板载 RGB 设色、读回、恢复 |
| Flow | `flow_send/status/clear`、`vibe_flow_label` | 手机/桌面注入文本，App 只读展示 |
| Voice | `vb_runtime_voice*`、`vibe_voice_label`、`vibe_voice_start`、`vibe_voice_clear` | 受控短录音、状态读取、bridge 导出 |
| Game helper | `vibe_2048_game`、`vibe_snake_autoplay` | 复杂交互放 Runtime 原生 helper |

## manifest 能力原则

- `capabilities` / `requires` / `permissions` 只能声明 Huangshan profile 能力。
- `weather.current` 表示 bridge 注入的数据，不表示板子自己联网。
- `flow.*`、`voice.*`、`display.*`、`power.*` 等细分能力用于 App UI 和工具校验。
- App 需要复杂逻辑时，先新增 Runtime helper，再把 helper 加入 Python/Swift 白名单。

## 明确拒绝的能力

当前 Huangshan profile 会拒绝这些 ESP32/GPL 侧能力：

- `wifi`
- `http`
- `network`
- `ntp`
- `board_ip`
- `native`
- `nes`
- `camera`
- `gamepad`
- `i2s`
- `audio`
- `bluetooth.pan`

拒绝原因不是永远不能做，而是当前产品边界要求联网和复杂外设先由手机/桌面 bridge
承担。等 Runtime API、权限模型、长稳和发布 gate 稳定后，再逐项评估。
评估矩阵见 [`runtime-high-risk-capabilities-evaluation.md`](runtime-high-risk-capabilities-evaluation.md)。

## 新增能力检查清单

新增能力时至少同步这些位置：

- 板端实现和 `vb_runtime_capabilities` JSON。
- `scripts/runtime_package.py` 的 manifest/helper 白名单。
- `mobile/ios/VibeBoardBLE/.../RuntimePackage.swift` 的同等白名单。
- 示例 App：`scripts/runtime_apps/<app_id>/manifest.json` 和 `main.lua`。
- serial/BLE regression 或 Swift test。
- 文档：本文件和必要的开发经验记录。

新增能力前先回答三个问题：

- 这个能力是否属于黄山派硬件事实，还是应由 host bridge 提供？
- 失败时 Runtime 能否降级，而不是崩溃或卡死？
- App 能否通过 manifest 权限、离线校验和版本握手明确表达依赖？
