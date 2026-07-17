# Codex Pet Bridge 与 pet/v1

更新时间：2026-07-15

## 进程边界

`scripts/codex_pet_bridge.py` 是电脑端唯一允许持有黄山派 Runtime BLE 连接的进程。
Bridge 只创建一个 `BLETransport`，所有设备命令经过 `TransportCommandQueue` 串行执行。
语音、App Store 和后续 MCP 不能再各自创建 BLE 连接，必须通过 Bridge 本地 IPC 提交请求。

默认资源：

- Unix Socket：`/tmp/huangshan-codex-pet-<uid>.sock`，权限 `0600`。
- BLE 缓存：`~/.vibeboard/codex_pet_ble.json`。
- 任务日志：`~/.vibeboard/codex_pet_tasks.json`，权限 `0600`。
- 硬件审计：`~/.vibeboard/codex_pet_hardware_audit.jsonl`，权限 `0600`；只保存参数
  SHA-256，不保存参数和设备响应正文。

启动命令：

```sh
python3 scripts/codex_pet_bridge.py --workspace /absolute/project/path
```

第二个 Bridge 不得删除或接管正在监听的 socket。只有确认 socket 已失效时，进程才会清理
遗留的 socket 文件。

## pet/v1 包络

Python 内部使用可读字段，BLE Flow 线上使用紧凑 JSON。普通板端包的硬上限为 192 字节。

| 内部字段 | wire 字段 | 规则 |
| --- | --- | --- |
| version | `v` | 固定为 `pet/v1` |
| kind | `k` | `action/ack/event/heartbeat/state` 的单字符码 |
| sequence | `s` | `1..4294967295`，单连接内递增 |
| message_id | `i` | 1 到 40 个安全 ASCII 字符，作为幂等 ID |
| timestamp_ms | `t` | 发送方 Unix 毫秒时间 |
| ttl_ms | `l` | `1..30000`；到期消息必须丢弃 |
| task_id | `q` | 可选 Codex thread ID |
| payload | `p` | 紧凑键值对象 |

状态、子类型和动作枚举在线路上使用固定短码，解码后恢复为完整名称。完整 Codex UUID、
`needs_input/credential` 和 24 字符详情组成的最坏状态包为 181 字节。

Bridge 到板端使用两个 Flow channel：

- `pet.state`：任务和本地语音状态。
- `pet.heartbeat`：连接心跳。

心跳每 10 秒发送一次，TTL 固定为 30 秒。板端不能依赖“断开通知”；最后一个有效心跳
到期时必须自行降级到 `disconnected` 并清除临时灯效。

## 状态归并

`EventReducer` 保存未过期事件，并从多任务事件中选择当前展示状态。优先级从高到低为：

```text
blocked > needs_input > transcribing > listening > running > ready > connected > disconnected
```

同一优先级选择序号更新的事件。重复 `message_id`、已过期事件和无效状态不会改变当前
展示。事件过期后重新归并；没有有效事件时结果固定为 `disconnected`。

## 动作与 ACK

本地 IPC 每行传输一个 pet/v1 JSON 包络，响应也是一行包络。阶段 1 开放两种动作：

- `ping`：验证 Bridge IPC。
- `submit_prompt`：提交 `text`，可选 `threadId` 用于继续已有任务。

ACK 状态：

- `accepted`：动作已被接受；对于语音任务，表示 app-server 已接受 `turn/start`。
- `duplicate`：相同 `message_id` 已处理或已预留，绝不自动重放。
- `rejected`：过期、乱序、无效、越权 workspace 或提交失败。

IPC 可以承载超过 192 字节的语音转录文本，但 Bridge 发往板端的状态包仍严格执行
192 字节限制。调用方必须使用项目绝对路径启动 Bridge；请求不能切换到其他 workspace。

## 崩溃与幂等

`submit_prompt` 调用 Codex 前，Bridge 先同步写入 `message_id` 和 `inputSha256`。任务日志
不保存转录正文、API Key 或 app-server 错误正文。

记录状态按以下顺序推进：

```text
reserved -> thread_allocated -> submitted
                               -> failed
```

如果进程在任意一步崩溃，重启后相同 `message_id` 返回 `duplicate`，不会猜测上一次调用
是否成功，也不会重发 turn。需要重试时必须由用户产生新的动作 ID。

## 自测

```sh
python3 scripts/codex_pet_protocol.py --self-test
python3 scripts/codex_pet_bridge.py --self-test
```

Bridge 自测会创建临时 Unix Socket；受限 sandbox 若禁止 `bind`，需要在允许本地 Unix
Socket 的测试环境运行。测试覆盖单实例、并发串行化、重复/乱序/过期请求、持久化
不重放、失败不重试、心跳断线重连和非 socket 路径保护。

## 板端语音任务

`scripts/runtime_apps/codex_pet/` 通过 `vibe_codex_pet()` 启动独立原生 helper
`vb_runtime_codex_pet.c`。helper 负责宠物 UI、触摸/K2 按住录音、上滑取消、新任务/继续
任务和 Flow 状态消费；Runtime `main.c` 只提供语音与按键回调。

语音链路：

```text
pet.new / pet.continue voice context
  -> mu-law BLE Notify
  -> CodexPetVoiceService
  -> GLM-ASR-2512
  -> pet.transcript
  -> submit_prompt
  -> pet.task.ack 或 pet.task.error
```

`VoiceStreamCollector` 和 mu-law 解码位于 `voice_bridge_common.py`，Pager 与 Codex Pet
共用实现。动作 ID 由录音序号和音频 SHA-256 摘要生成；Bridge 在录音被清除前崩溃时，
重启处理相同捕获也不会创建重复 turn。上滑取消或 K2 松开前清除捕获时没有 ready 录音，
因此不会调用 ASR 或 Codex。

默认 ASR 临时 WAV 在调用完成后删除。只有显式传入 `--save-voice-captures` 才写入
`captures/codex_pet/`。ASR/provider 错误正文不会下发到板端或写入任务日志。

相关 Bridge 参数：

```text
--asr-model glm-asr-2512
--asr-prompt <text>
--save-voice-captures
--voice-capture-dir <path>
--no-open-thread
--open-desktop-thread       # 可选；仅在完成时聚焦桌面任务，默认关闭
--mock-transcript <text>   # 仅开发验证
```

真机测试后台使用 `scripts/codex_pet_test_backend.command` 启动。GLM API Key 保存在 macOS
登录钥匙串的 `com.huangshan-pi.codex-pet.glm-asr` 条目中，启动时自动读取，不写入项目、
环境配置或测试日志。首次启动会由 `security` 命令进行隐藏式输入；后续直接启动即可。

```sh
# 替换已保存的 Key，并启动后台
./scripts/codex_pet_test_backend.command --replace-api-key

# 从钥匙串删除 Key，不启动后台
./scripts/codex_pet_test_backend.command --forget-api-key
```

## 后台实时控制台

Bridge 自己就是提交 turn 的 Codex app-server 客户端，因此测试后台终端现在直接显示同一
连接上的事件，不依赖桌面版深链缓存：

- 请使用标题为 `Codex Pet LIVE Console` 的运行中终端。显示“进程已完成”的旧终端不是
  实时窗口，可以关闭。

- `item/agentMessage/delta`：实时打印 Codex 回复增量。
- `turn/completed`：打印任务完成状态和任务短 ID。
- `item/commandExecution/requestApproval`、`item/fileChange/requestApproval`：打印不含命令
  参数和路径正文的审批摘要；板端仍可完成一次性批准/拒绝。
- `item/tool/requestUserInput`：只提示需要手动输入或 API Key，不打印问题正文或密钥。

默认不自动打开 Codex 桌面任务，避免桌面版已有页面只聚焦旧缓存而造成“回复消失”的错觉。
如需兼容旧行为，显式增加 `--open-desktop-thread`；它不参与任务提交和审批处理。
Codex 桌面 App 与 Bridge 各自运行独立 App Server；终端实时输出不等于桌面任务实时刷新。

Bridge 每 10 秒重发项目名和可继续任务能力。板端 30 秒没有有效心跳时自动进入
`disconnected`；若当时仍在录音或等待转录，会先清理语音捕获。

Flow 序号以当前毫秒的低 32 位为进程起点，并在进程内单调递增。板端用 32 位串行号差值
判断新旧，因此快速重启 Bridge 不会让审批、状态或额度消息被误判为旧包，计数器回绕后也
能继续接收。不要改回秒级种子或直接使用 `sequence <= last_sequence` 比较。

## MCP 硬件工具

`scripts/codex_pet_mcp.py` 实现 MCP `2025-06-18` stdio JSON-RPC。标准输出只承载一行
一个 JSON-RPC 消息；诊断信息只写标准错误。MCP 进程不持有 BLE，而是把
`hardware_command` 通过 Bridge 的 `0600` Unix Socket 交给同一命令队列。

开放范围：

- 只读：连接状态、能力、传感器、电量、RGB、显示、当前 App、已安装 App、可选音频状态。
- 有副作用：设置命名 RGB 色、设置 `0..100` 亮度、显示最多 160 字节消息、启动已安装
  且 ID 合法的 App、播放 5 个内置提示音、停止播放。
- 永不开放：raw GPIO、任意文件读写、固件刷写、安装/删除 App、后台麦克风录制。

提示音只接受 `listening/submitted/needs_input/done/error` 枚举并固定映射到
`codex_pet/assets/*.wav`；MCP 不能提交路径或音频字节。录音保留 codec 时播放返回 busy。

所有参数要求字段精确匹配；非法字段、颜色、亮度或 App ID 会在 Bridge 内再次拒绝，
不能依赖 MCP 客户端的 schema 校验。副作用操作执行前必须成功写入审计元数据。

项目配置模板位于 `docs/codex-pet-mcp.toml`。它包含绝对解释器、脚本路径和
`enabled_tools` 白名单，但不会自动修改 `~/.codex/config.toml`。先启动 Bridge，再由用户
审阅并合并模板。离线验证：

```sh
python3 scripts/codex_pet_mcp.py --self-test
```

## 板端实体审批

只有通过本 Bridge 提交且已写入任务日志的 Codex thread，才能在板端显示可操作审批。
支持的方法严格限定为：

- `item/commandExecution/requestApproval`
- `item/fileChange/requestApproval`

权限扩张、API Key/credential、普通问题、MCP elicitation 和其他 Codex 任务只显示电脑操作
提醒，不能由板端响应。命令审批只发送可执行文件名（例如 `git command`），不发送参数、
路径正文或环境变量；文件审批只显示 `file change`。

电脑向板端发送不超过 184 字节的 `pet.approval`，板端临时显示 `Approve / Deny`。点击后
板端通过加密且已订阅通知的 BLE 状态特征发送：

```text
pet_action action=approve request=<one-time-id>
pet_action action=deny request=<one-time-id>
```

电脑端以锁保护取出一次性请求，先删除再调用 app-server `respond`；重复点击、迟到响应和
错误 ID 都返回未处理，不可能命中其他审批。默认 60 秒超时，超时或服务退出会发送
`{"decision":"decline"}`。若板端发布失败，请求保留在 Codex 电脑 UI 中处理。
