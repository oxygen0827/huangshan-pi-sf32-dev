# Codex Pet Companion

**把 Codex Desktop 的 AI 工作状态，实时呈现在桌面上的硬件宠物里。**

黄山派放在你的桌子上，当 Codex 在帮你写代码、跑工具调用、等你审批命令，屏幕上的宠物会随时告诉你它在干嘛——不用盯着电脑屏幕，也能感知 AI 的工作状态。需要审批操作时，直接按板子上的物理按键确认，不用切回窗口。

---

## 它是如何工作的

Codex Desktop App（ChatGPT.app）通过两条通路与黄山派通信：

**Codex Hooks** — Codex 执行任务的每个关键节点都会触发一个 Hook 事件，`codex_pet_hook.py` 接收事件后经由 Unix socket 发给 Bridge 进程，Bridge 再把当前状态通过 BLE 推到板子。

**MCP 工具** — Bridge 同时注册了一套 `huangshan_pet` MCP 工具，让 Codex 本身也能主动查询板子状态、控制 RGB 灯、切换宠物、播放提示音、在屏幕上显示消息。

```
Codex Desktop App（ChatGPT.app）
  ├─ Hooks：SessionStart / UserPromptSubmit / PreToolUse
  │          PostToolUse / PermissionRequest / Stop
  │          → codex_pet_hook.py → Unix socket
  │
  └─ MCP：huangshan_pet 工具集（查询 / 控制）
           → codex_pet_mcp.py → Unix socket

codex_pet_bridge.py（唯一 BLE 持有者）
  → BLE pet/v1 协议
  → 黄山派 Codex Pet App
  → 宠物动画 + 任务状态 + Allow / Deny
```

---

## 宠物状态

每只 Petdex 宠物包含九种动画：`idle`、`runRight`、`runLeft`、`waving`、`jumping`、
`failed`、`waiting`、`running`、`review`。Codex 任务状态映射如下：

| 状态 | 触发条件 | 宠物表现 |
|---|---|---|
| `blocked` | 任务出错或被中断 | 困惑/受挫动画 |
| `needs_input` | Codex 发起审批请求 | 等待动画，按键变为 Allow/Deny |
| `running` | 工具调用进行中 | 跑动/工作动画 |
| `ready` | 任务完成（Stop） | 欢呼/庆祝动画 |
| `connected` | Codex 会话已打开 | 待机动画 |
| `disconnected` | Bridge 心跳中断 | 静止，RGB 熄灭 |

Bridge 内部还有 `listening`（录音中）和 `transcribing`（ASR 转写中）两个状态，这两个状态在当前 monitor 模式下不会激活。

---

## 每板一宠物

板子**同时只有一只 active 宠物**。九个状态每个至少 2 帧、最多 8 帧，转换为 160x173
RGB565A、120ms 的 `VBPC v2` 资源。板端只解压并缓存当前状态，状态切换不需要反复读取整包。

换宠物需要：在桌面端重新生成 `preload.bin`，然后把 Codex Pet App 重新安装到板子。这个操作可以通过 Companion 网页一键完成，不需要重新烧录固件。

---

## 交互

| 操作 | 常规状态 | 待审批状态 |
|---|---|---|
| 屏幕向右滑 | 切换到上一个任务 | - |
| 屏幕向左滑 | 切换到下一个任务 | - |
| 点击宠物 | 播放跳跃动作 | - |
| 审批键 | - | Allow / Deny |

审批只对以下两类操作有效，其余一律显示"请在电脑端处理"：
- 命令执行（`item/commandExecution/requestApproval`）
- 文件修改（`item/fileChange/requestApproval`）

---

## 核心文件

```
scripts/
  codex_pet_bridge.py          主进程（BLE bridge + IPC server + Companion API）
  codex_pet_monitor.py         Monitor 模式的 Codex Desktop 事件处理
  codex_pet_hook.py            Codex Hooks 侧事件接收脚本
  codex_pet_mcp.py             MCP 工具服务（通过 Bridge IPC 转发硬件命令）
  codex_pet_companion.py       宠物图库与一键部署服务（端口 8790）
  codex_pet_web.html           Companion 网页 UI
  codex_pet_agent.py          打包后的统一 Agent / Hook 入口
  codex_pet_companion_app.swift macOS 菜单栏外壳
  build_codex_pet_companion_app.command 自包含 App、DMG、签名与公证

scripts/runtime_apps/codex_pet/
  main.lua                     板端入口（调用原生 helper）
  manifest.json                Runtime App 清单
  assets/pets/                 宠物帧（本地生成）
  assets/*.wav                 提示音（listening / submitted / needs_input / done / error）

docs/
  codex-pet-bridge.md          协议、状态归并、审批安全边界、事故复盘
  codex-pet-one-click-deploy.md 宠物部署架构与 .hpet 格式
  codex-pet-hooks.json         Hooks 配置模板
  codex-pet-mcp.toml           MCP 配置模板
```

---

## 相关文档

- [QUICKSTART.md](QUICKSTART.md) — 从开机到宠物上板的完整开箱流程
- [../docs/codex-pet-bridge.md](../docs/codex-pet-bridge.md) — 协议细节、状态归并逻辑、事故复盘
- [../docs/codex-pet-one-click-deploy.md](../docs/codex-pet-one-click-deploy.md) — 宠物部署架构与安装恢复
- [../docs/macos-companion-release.md](../docs/macos-companion-release.md) — macOS 打包、域名、签名、公证与发布 Gate
- [FIRMWARE-UPDATES.md](FIRMWARE-UPDATES.md) — Codex Pet 固件检查、签名发布与未来 BLE DFU 边界
