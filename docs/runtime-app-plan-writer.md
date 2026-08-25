# 黄山派 Runtime App Plan Writer

更新时间：2026-07-12

`scripts/runtime_app_plan_writer.py` 把一个受控 JSON plan 生成黄山派 Runtime App
包。它面向 AI 生成和开发者快速草稿：默认只生成 `manifest.json`、`main.lua`
和 `README.md`，并复用 `scripts/runtime_package.py` 做路径、manifest、Lua 安全边界、
能力白名单和 integrity 校验。

## 推荐：组件化 UI plan

AI 生成普通信息页、状态页和硬件面板时，优先选择 `ui.modules`，不要自行计算
每个 LVGL 控件的坐标。writer 会调用固件内置的 `huangshan-ui/v1`，自动安排
390x450 安全区、三列指标、状态徽标、进度和最多两个操作按钮；超出高度预算会
直接拒绝生成。

```json
{
  "app": {
    "id": "system_pulse",
    "name": "System Pulse",
    "description": "Live board health",
    "capabilities": ["display", "power", "touch"]
  },
  "ui": {
    "theme": "system",
    "modules": [
      { "type": "header" },
      { "type": "metric", "label": "Battery", "capability": "power.battery", "status": "ok" },
      { "type": "metric", "label": "Touch", "capability": "touch.count" },
      { "type": "progress", "label": "Readiness", "value": 84, "status": "ok" }
    ]
  }
}
```

支持模块：`header`、`metric`、`badge`、`progress`、`button`。`metric.capability`
会自动选择已有的 sensor、touch、GPIO、power、display、voice、flow 或 audio
绑定。按钮目前只开放已验证的 `audio.tone` 和 `audio.stop` 动作。

完整可运行示例见
[`ai-ui/runtime-dashboard-plan.json`](ai-ui/runtime-dashboard-plan.json)。

## 兼容：manifest components

```json
{
  "app": {
    "id": "plan_demo",
    "name": "Plan Demo",
    "description": "Generated locally from a Huangshan Runtime app plan.",
    "category": "Tools",
    "icon": "sparkles",
    "author": "Runtime App Plan Writer",
    "requirements": ["Runtime", "Display"],
    "capabilities": ["display"]
  },
  "components": [
    { "type": "label", "text": "Generated safely", "color": "#5eead4" }
  ]
}
```

没有 `ui`、`main.lua` 或 `mainLua` 时，writer 仍会按旧规则生成保守的
`vibe_label(...)` 页面。需要游戏或特殊交互时，可以显式提供完整 `mainLua`；
常规页面不应跳过组件库。

## 带文件的 plan

```json
{
  "app": {
    "id": "asset_demo",
    "name": "Asset Demo",
    "description": "Reads a packaged text asset.",
    "screenshot": "generated:asset_demo"
  },
  "mainLua": "vibe_label(\"Asset Demo\", 30, 60, 330, LV_ALIGN_TOP_LEFT, 0xf8fafc)\nvibe_read_file(\"assets/note.txt\")\n",
  "files": [
    {
      "path": "assets/note.txt",
      "content": "hello from package\n"
    }
  ]
}
```

文件路径仍受 Runtime 包格式白名单限制。二进制资源可以使用
`"encoding": "base64"`。

## CLI

只生成目录：

```bash
python3 scripts/runtime_app_plan_writer.py plan.json
```

生成目录并输出 `.happ`：

```bash
python3 scripts/runtime_app_plan_writer.py plan.json --package
```

指定输出目录：

```bash
python3 scripts/runtime_app_plan_writer.py plan.json --output-root generated/runtime_apps --package
```

自测：

```bash
python3 scripts/runtime_app_plan_writer.py --self-test
```

## Web App Store

本地 App Store 的 `AI App Plan` 面板可以直接粘贴 plan JSON。点击“生成并校验”
后，服务端会调用同一个 writer，并把生成结果放进现有导入包流程：

```text
JSON plan -> 生成 Runtime package -> validate_package -> import token -> 安装导入包
```

这意味着 Web 生成包、拖入 `.happ` 包和 CLI 生成包都共享同一套 Huangshan
profile、Lua 安全边界、manifest integrity 和截图引用校验。

## 边界

- 默认 profile 是 `huangshan-pi`。
- `audio`、`audio.playback`、`lua.full` 已允许；`wifi`、`http`、`network`、
  `camera`、`native`、`nes`、`gamepad`、`i2s` 等能力仍会被拒绝。
- writer 可以接收完整 Lua 语言代码，但不会绕过 Runtime helper、文件和资源配额。
- `huangshan-ui/v1` 是固件内置 API；生成包会在 manifest 写入 `uiKit` 和
`uiModules`，便于 App Store 和审查工具识别。
- `metric.capability` 只接受能在 104px 指标卡内清晰呈现的标量值；三轴
  accelerometer/gyro/magnetometer 数据应使用自定义全宽区域，writer 会拒绝把它们
  塞进指标卡。
- `manifest.json` 由 writer 生成；plan 中同名文件不会被信任。

这些能力的暂缓原因和重新评估条件见
[`runtime-high-risk-capabilities-evaluation.md`](runtime-high-risk-capabilities-evaluation.md)。
