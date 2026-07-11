# 黄山派 Runtime App Plan Writer

更新时间：2026-07-10

`scripts/runtime_app_plan_writer.py` 把一个受控 JSON plan 生成黄山派 Runtime App
包。它面向 AI 生成和开发者快速草稿：默认只生成 `manifest.json`、`main.lua`
和 `README.md`，并复用 `scripts/runtime_package.py` 做路径、manifest、Lua subset、
能力白名单和 integrity 校验。

## 最小 plan

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

如果没有提供 `main.lua`，writer 会生成一个保守的 Lua 入口，只使用
`vibe_label(...)` 这类高层 helper。复杂 App 应该在 plan 里显式提供 `mainLua`
或 `files[]` 中的 `main.lua`。

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
profile、Lua subset、manifest integrity 和截图引用校验。

## 边界

- 默认 profile 是 `huangshan-pi`。
- `wifi`、`http`、`network`、`camera`、`native`、`nes`、`gamepad`、`i2s`、
  `audio` 等 ESP32/GPL 高阶能力会被包校验拒绝。
- writer 不会生成完整 Lua VM 代码，也不会绕过 Runtime helper 白名单。
- `manifest.json` 由 writer 生成；plan 中同名文件不会被信任。

这些能力的暂缓原因和重新评估条件见
[`runtime-high-risk-capabilities-evaluation.md`](runtime-high-risk-capabilities-evaluation.md)。
