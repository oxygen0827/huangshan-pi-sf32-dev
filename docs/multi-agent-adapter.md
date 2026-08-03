# 多 Agent 适配层

更新时间：2026-08-03

Codex Pet 最初只监控 Codex Desktop。本适配层把"某个 AI Agent 的生命周期 Hook 事件"与
"板端任务状态"解耦，让 Claude Code（以及后续 Cursor / Cline / Gemini CLI）都能复用同一条
Bridge / Monitor / BLE 管线，而不需要改动 Bridge、Monitor 或 `pet/v1` 协议。

## 为什么能做到"零 Bridge 改动"

关键在于 `codex_pet_monitor.py` 的 `CodexDesktopMonitor.handle()` 只消费与 Agent 无关的
字段：`session_id`、`status`、`project`、`detail`、`event`。任何能产出同样形状 `hook_event`
包络的来源，都能直接流过现有管线：

```text
<agent> 生命周期 Hook (stdin JSON)
  -> scripts/agent_hook.py --agent <id>
  -> AgentAdapter.build_envelope()  (归一化为 pet/v1 hook_event)
  -> Bridge 本地 Unix socket (与 Codex 同一个 socket)
  -> monitor_handler -> CodexDesktopMonitor.handle()
  -> DesktopTaskRegistry.apply_hook()
  -> pet.tasks BLE 快照 -> 黄山派宠物动画 + 状态
```

因此新增一个 Agent = 实现一个 `AgentAdapter` 子类并注册，Bridge / Monitor / 固件都不动。

## 组成

- `scripts/agent_adapters.py`：适配层核心。`AgentAdapter` 基类、`CodexAdapter`、
  `ClaudeCodeAdapter`、注册表 `ADAPTERS`、配置生成 `render_hook_config()`，以及 `--self-test`。
- `scripts/agent_hook.py`：统一 Hook 入口。`--agent {codex,claude_code}`、`--print-config`、
  `--self-test`、`--strict`、`--print-ack`。读取 stdin 的一条 Hook JSON，归一化后发往 Bridge。
- `docs/claude-code-hooks.json`：Claude Code 的 `~/.claude/settings.json` 片段模板。

`CodexAdapter` 直接委托给现有且已审计的 `codex_pet_hook.hook_envelope()`，保持 Codex 行为
逐字节不变——它仍是 Codex 事件映射的唯一事实来源。既有 Codex 用户的 `~/.codex/hooks.json`
继续指向 `codex_pet_hook.py`，不受影响。

## 单一 Bridge，多 Agent 共存

Claude Code Hook 与 Codex Hook 发往**同一个** Bridge socket
（`CODEX_PET_SOCKET`，默认 `/tmp/huangshan-codex-pet-<uid>.sock`）。因此只要 Bridge 在运行
（用现有 `scripts/codex_pet_monitor.command` 启动即可），两个 Agent 的任务会聚合进同一个
任务注册表，一只宠物同时展示你所有 Agent 的运行 / 等待 / 完成状态。任务用 `session_id`
区分，用 `project`（cwd 目录名）标注归属。

## 接入 Claude Code

1. 保持 Bridge 运行：`./scripts/codex_pet_monitor.command`。
2. 生成与本机路径匹配的配置片段：

   ```sh
   .venv/bin/python scripts/agent_hook.py --agent claude_code --print-config
   ```

3. 把片段中的 `hooks` 合并进 `~/.claude/settings.json`（或参照
   `docs/claude-code-hooks.json`）。与 Codex 一样，我们不自动改写用户配置——由用户审阅后合并。
4. 重开 Claude Code 会话使 Hook 生效。板子应随任务进入 `running / needs_input / ready`。

## Claude Code 与 Codex 的差异

| 维度 | Codex | Claude Code | 适配层处理 |
| --- | --- | --- | --- |
| 配置位置 | `~/.codex/hooks.json` | `~/.claude/settings.json` | 各 Adapter 声明 `config_target` |
| 审批事件 | 独立 `PermissionRequest` | `Notification`（message 含 "permission"） | 仅权限类 Notification 归一化为 `PermissionRequest` |
| turn ID | 有 `turn_id` | 无 | Claude Code 侧传 `None`，`apply_hook` 已兼容 |
| 空闲提示 | 无 | 空闲类 `Notification` | 忽略（无板端状态意义） |
| 生命周期收尾 | `Stop` | `Stop` / `SubagentStop` / `SessionEnd` | `Stop`/`SubagentStop` → `ready`；`SessionEnd` 忽略，交由保留期自然过期 |

## 审批边界（v1：通知优先）

Claude Code 第一版是 **notify-only**：权限提示在板端显示黄色 `needs_input` 和
"Approval required"，但**不显示 Allow / Deny 按键**，请在电脑端处理。

这与现有安全边界天然一致：板端是否出现审批按键，只取决于任务快照里有没有真实的一次性
审批 ID，而这个 ID 只在 Monitor 收到 `approvalClass == "managed_action"` 且任务受 Bridge
管理时才创建。`ClaudeCodeAdapter` **从不**设置 `approvalClass`，所以按键不会出现——无需任何
Bridge 改动即为 fail-closed。板端物理审批（利用 Claude Code `PreToolUse` 的
`permissionDecision` 返回值）需要阻塞式 Hook 设计，留作第二步。

## 隐私

归一化时只转发 Adapter 自己构造的安全字段：cwd 的目录名（`project`）、短工具名、固定
状态文案。Agent 的 prompt、命令参数、`tool_input`、`message` 正文、transcript 路径一律不进入
包络。`agent_adapters.py --self-test` 用带密标记的输入断言这些正文不出现在编码结果中。

## 加一个新 Agent（例如 Cursor）

1. 在 `agent_adapters.py` 新增 `CursorAdapter(AgentAdapter)`：设置 `id / display_name /
   config_target / hook_events`，实现 `build_envelope()`（把该 Agent 的事件词汇翻译成
   `codex_pet_hook.hook_envelope()` 认识的 `hook_event_name` 后委托，或直接构造 `PetEnvelope`）。
2. 加进 `ADAPTERS` 注册表。
3. 在 `self_test()` 增加该 Agent 的归一化与隐私断言。
4. 用户用 `agent_hook.py --agent cursor --print-config` 生成配置。

无需改动 Bridge、Monitor、协议或固件。

## 验证

```sh
python3 scripts/agent_adapters.py --self-test
python3 scripts/agent_hook.py --self-test
python3 scripts/codex_pet_hook.py --self-test      # Codex 行为回归，必须仍然通过
python3 scripts/codex_pet_protocol.py --self-test
```

真机联调建议：启动唯一 Bridge，接入 Claude Code Hook 后，在 Claude Code 里跑一个会用工具
的任务，确认板端依次进入 `running`（蓝）、工具调用文案、`ready`（绿）；触发一次权限提示，
确认板端显示黄色 `needs_input` 且没有 Allow / Deny 按键。
