# Codex Pet Bridge 与 pet/v1

更新时间：2026-07-20

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

Bridge 到板端使用这些 Flow channel：

- `pet.state`：任务和本地语音状态。
- `pet.heartbeat`：连接心跳。
- `pet.tasks`：所选任务、可见任务总数和正在执行的任务数。
- `pet.approval`：受限审批请求及一次性 ID。
- `pet.select`：从 Codex/MCP 选择板端宠物，payload 是不超过 24 字符的安全 slug。

心跳每 10 秒发送一次，TTL 固定为 30 秒。板端不能依赖“断开通知”；最后一个有效心跳
到期时必须自行降级到 `disconnected` 并清除临时灯效。

桌面任务来自 durable task journal 和 Monitor 收到的 Hooks；Bridge 会恢复启动前已保存的
任务快照，不扫描桌面之外的任务。任务快照中的 `n` 是 Monitor 已观察到且仍保留的任务总数，
`ac` 只统计 `running / needs_input / blocked`。
完成任务可以继续显示为 Ready，但不会计入 active。首个 Hook 前的空状态为
`Waiting for Codex events`，收到 Hook 后没有 active 任务时显示 `No active tasks`。

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
不重放、失败不重试、心跳断线重连、断开的日志 pipe 不得中断连接，以及 IPC handler
异常必须返回合法 `pet/v1` 拒绝 ACK。

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

## 恢复与 BLE 重连

Bridge 启动时先加载 `~/.vibeboard/codex_pet_tasks.json` 和最近的状态/审批快照，连接通过
Codex Pet ready gate（`active=1`、至少一个宠物、帧数大于 0、Flow 队列为空，并连续观察到
`uiTicks` 前进）后，再按顺序回放任务、审批和宠物选择。冷启动 gate 最多等待 30 秒，覆盖
Petdex 压缩 idle 动画的 PSRAM 预载。Monitor 启动时不做可选的项目广播，
避免启动阶段的 BLE 队列竞争；任务 Hook 到达后才发送新的项目/任务快照。

Monitor 使用固定 CoreBluetooth 外设标识做身份校验，但每次连接先扫描到同一标识的实时广告
对象，再建立 GATT 连接。这是为了绕过 macOS 在板子复位后缓存的旧服务表；地址不匹配仍会
拒绝连接。`runtime_transport.py --self-test` 覆盖缓存名称和连接协议，真机 soak 覆盖连接、
通知、状态读取和重连。

Bridge 为 `SIGINT` 和 `SIGTERM` 安装统一 shutdown handler。正常重启会先停止 Hook/审批任务、
关闭 IPC、退订 Notify 并断开 BLE，再退出进程；不要用 `kill -9` 作为日常重启方式，否则只能
依赖板端链路超时恢复。连接阶段会在 Monitor 日志记录 `connect attempt`、失败类型和
`connected and ready after <ms>`，用于区分扫描、GATT 和 ready gate 延迟。

`TransportCommandQueue` 对普通 Runtime/GATT 命令使用 15 秒 deadline，连接/验证最多 40 秒，
关闭最多 8 秒。命令超时后队列立即拒绝已排队请求并进入恢复状态，只允许 `close` 和
`connect`；新连接成功后才重新开放普通命令。这样 CoreBluetooth 半开 await 不会永久占住唯一
worker，MCP 超时后心跳仍能断开、重新扫描、恢复 Notify 并回放快照。

板端 Codex Pet 不在 BLE 事件线程或 GUI 线程写持久化文件。`pet.tasks`、`pet.heartbeat` 和
`pet.select` 只更新 RAM；任务跨板子复位由 Bridge durable journal + reconnect replay 恢复。
通用 Runtime App 的非 `pet.*` flow 仍按原协议写入 `/sdcard/apps/.flow`，因此现有 flow_stage
持久化行为不变。

Flow 序号以当前毫秒的低 32 位为进程起点，并在进程内单调递增。板端用 32 位串行号差值
判断新旧，因此快速重启 Bridge 不会让审批、状态或额度消息被误判为旧包，计数器回绕后也
能继续接收。不要改回秒级种子或直接使用 `sequence <= last_sequence` 比较。

## 2026-07-20 事故复盘：Bridge 进程存在但板端显示未连接

### 现象

板端显示未连接，任务和运行动画不再更新，但 USB 串口仍存在。进程列表中也能看到
`codex_pet_bridge.py --mode monitor`，容易误判为板子、BLE 地址或 GATT 缓存故障。
最小真机检查持续得到空状态，原始 IPC 响应为：

```json
{"ok":false,"error":"internal_error:BrokenPipeError"}
```

与此同时，串口 `vb_runtime_status` 明确返回 `running=1`、`active=codex_pet`，宠物帧和提示音
也已预载。这证明板子固件没有卡死，故障位于电脑端 Bridge 生命周期。

### 根因链

1. Bridge 曾被直接启动在临时 Codex 命令执行会话中，而不是由
   `scripts/codex_pet_monitor.command` 的可见 Terminal 持有。
2. 临时会话结束后父进程退出，Bridge 子进程继续存活并被系统重新收养；它的
   stdout/stderr 仍指向原执行会话创建的 pipe，但读取端已经消失。
3. `DeviceSession._connect()` 在真正调用 BLE `connect` 前先执行带 `flush=True` 的日志输出，
   因此首先抛出 `BrokenPipeError`，BLE 重连实际从未开始。
4. 硬件请求的异常处理再次向同一个坏 stderr 写日志，使原本可返回的
   `hardware_failed` ACK 也被 `BrokenPipeError` 打断。
5. `LocalIPCServer` 把未处理异常编码为裸 `{ok,error}` JSON；MCP 只接受 `pet/v1` 包络，
   最终只能报告无效响应，界面降级为未连接。

旧现场的关键组合证据是：Bridge 为无监督的直接 Python 进程、父进程已经消失、标准输出和
标准错误仍是没有读取者的 pipe。`PPID=1` 单独不能证明故障；合法的 `launchd` 服务也可能
由 PID 1 持有，但必须有明确 supervisor 和有效的文件/系统日志目标。

### 修复和长期规则

- Bridge 进程入口用 `NonFatalTextStream` 包装 stdout/stderr。终端、`tee` 或日志读取端消失时，
  日志可以丢弃，但 BLE、心跳、Hook 和 IPC 控制路径不得失败。
- 本地 IPC 在成功解析请求后，handler 的任何异常都返回合法 `pet/v1` `rejected` ACK；
  裸 `{ok,error}` 只允许用于连请求包络都无法解析的情况。
- MCP 保留 ACK 中经过长度约束的错误码，不再把协议内拒绝误报为 `invalid response`。
- 日常启动只使用 `./scripts/codex_pet_monitor.command`。不得把需要长期运行的 Bridge 留在
  临时 agent shell、一次性 PTY 或会在任务结束时回收 reader 的命令会话中。
- “进程存在”不等于“连接健康”。验收必须同时确认唯一 Bridge 进程、socket 可访问、日志出现
  `connected and ready`、`huangshan_pet_status.connected=1`，并且两次读取之间 `uiTicks` 前进。
- 重启旧 Bridge 使用 `SIGTERM`，等待 socket 正常删除后再启动新实例；日常不使用
  `kill -9`，也不手工删除仍被活跃进程持有的 socket。

### 排障和回归

先检查进程所有权和日志管道：

```sh
ps -axo pid,ppid,stat,lstart,command | \
  rg 'codex_pet_bridge[.]py --mode monitor|codex_pet_monitor[.]command|tee .*huangshan_codex_pet_monitor'
lsof -nP -a -p <bridge-pid> -d 0,1,2
tail -100 "${TMPDIR:-/tmp}/huangshan_codex_pet_monitor.log"
```

正常 Terminal 启动应形成 `zsh monitor.command -> Python Bridge + tee`，Python 的输出 pipe
由 `tee` 读取，`tee` 再写终端和日志。随后运行与事故现场相同的最小真机回归：

```sh
python3 scripts/codex_pet_bridge.py --self-test
python3 scripts/codex_pet_mcp.py --self-test
PYTHONPATH=scripts .venv/bin/python scripts/codex_pet_soak.py \
  --storage-stress-cycles 1 --storage-stress-timeout 5
```

最后一条必须输出 `passed=true`、`newDroppedFlows=0`、`preloadedBytes>0`，并看到 `uiTicks`
前进。若串口正常而这一步仍失败，优先检查 Bridge 生命周期和 IPC 原始错误，不要先重刷固件。

## MCP 硬件工具

`scripts/codex_pet_mcp.py` 实现 MCP `2025-06-18` stdio JSON-RPC。标准输出只承载一行
一个 JSON-RPC 消息；诊断信息只写标准错误。MCP 进程不持有 BLE，而是把
`hardware_command` 通过 Bridge 的 `0600` Unix Socket 交给同一命令队列。

开放范围：

- 只读：连接状态、能力、传感器、电量、RGB、显示、当前 App、已安装 App、可选音频状态，
  以及板端 Codex Pet 的任务数、告警、动画和当前宠物状态。
- 有副作用：设置命名 RGB 色、设置 `0..100` 亮度、显示最多 160 字节消息、启动已安装
  且 ID 合法的 App、选择白名单内的板端宠物、播放 5 个内置提示音、停止播放。
- 永不开放：raw GPIO、任意文件读写、固件刷写、安装/删除 App、后台麦克风录制。

提示音只接受 `listening/submitted/needs_input/done/error` 枚举并固定映射到
`codex_pet/assets/*.wav`；MCP 不能提交路径或音频字节。五个 cue 在 Companion 启动时预载，
音频状态的 `cachedCues=5` 表示运行期播放不再读取 SD。录音保留 codec 时播放返回 busy。

所有参数要求字段精确匹配；非法字段、颜色、亮度或 App ID 会在 Bridge 内再次拒绝，
不能依赖 MCP 客户端的 schema 校验。副作用操作执行前必须成功写入审计元数据。

项目配置模板位于 `docs/codex-pet-mcp.toml`。它包含绝对解释器、脚本路径和
`enabled_tools` 白名单，但不会自动修改 `~/.codex/config.toml`。先启动 Bridge，再由用户
审阅并合并模板。离线验证：

```sh
python3 scripts/codex_pet_mcp.py --self-test
```

`huangshan_select_pet` 当前接受 `boba`、`boxcat` 或 `shinchan`，通过现有加密 BLE 连接发送
`pet.select`，不启动 Petdex Desktop，也不会在电脑桌面额外显示宠物。板端点击宠物切换时
会发送 `pet_action action=pet_select request=<slug>`，用于让常驻 monitor 记录当前选择。

`huangshan_pet_status` 是只读诊断工具，直接返回板端实际显示状态。关键字段包括：
`tasks`（保留的可见任务数）、`activeTasks`（正在执行、待人工输入或阻塞的任务数）、
`recentTasks`（近期完成的真实任务数）、`syncAgeMs`（距最近一次 Bridge 同步的毫秒数）、
`state`、`approval`、`indicator`、`pet`、`frames`、`frameMs` 和 `preloadedBytes`。后者必须
大于 0，表示所有运行时宠物帧已在启动阶段进入 PSRAM。它不返回任务正文、命令、
路径或审批内容。需要确认板子是否收到新任务时，优先查询这个工具，而不是根据桌面窗口推断。

## 验证清单

1. 启动唯一 Monitor 终端，看到 `Codex Pet monitor ready`；不要同时启动 App Store、语音
   Bridge 或另一个 BLE 客户端。
2. 运行 `huangshan_pet_status`，确认 `connected=1`、`active=1`、`tasks` 与
   `activeTasks` 正确，且 `frames>=2`、运行态 `frameMs=600`（其他状态 `1200`）、
   `preloadedBytes>0`、`loaderPhase=0`。Companion 启动后不再从 SD 读取宠物帧；源资源可以
   包含更多帧和更短的建议间隔。
3. 在 Codex 中开始一个任务，确认板端进入 `running`、RGB 变蓝、`activeTasks=1`；完成或
   等待输入时确认状态分别变为 `ready` 或 `needs_input`。
4. 通过 `huangshan_select_pet` 依次选择 `boxcat`、`boba`、`shinchan`，每次立即再次查询状态，
   确认 `pet` 和 `frames` 变化、`uiTicks` 持续增长；选择不会读写板端文件。
5. 运行 3 分钟 exercise soak，要求 `passed=true`、`exerciseFailures=0`、
   `animationErrors=0`、`openOutage=false`；串口同时检查 `fatal error`、`assertion failed`、
   `spi sem timeout` 均为 0。
6. 做一次板子复位或重新刷写，等待 Monitor 自动重连，确认之前的任务快照被回放；若 Mac
   使用了旧 GATT 缓存，Bridge 会按同一外设标识重新扫描，不需要手动删除其他设备。
7. 24 小时 soak：保持以下命令在独立终端运行，并记录 Mac 睡眠/唤醒、板子复位、BLE 断线
   的时间点；期间禁止启动第二个 BLE 客户端。完成后检查 JSONL 的 `passed`、`openOutage`、
   `failures`、`exerciseFailures` 和 RSS 增长。5 秒采样用于让 30 秒重连 SLA 有足够测量分辨率；
   exercise 选择宠物时每个 slug 最多重试三次，以覆盖 BLE 队列短暂拥塞，但最终仍要求
   `exerciseFailures=0`。`rssGrowthKiB` 是单个 Bridge PID 生命周期内的最大增长，适合判断
   内存泄漏；`rssTotalRangeKiB` 是跨 Bridge 重启的原始总范围，仅用于诊断不同进程基线。
   标准 24 小时启动器还要求 `sleepGaps>=1`、`reconnects>=2` 和 `exerciseCycles>=100`；未完成
   真实睡眠/唤醒、板端复位、Bridge 重启或足够的多任务循环时不得输出 `passed=true`。
   `sleep-gap` 使用“墙钟经过时间减去 `CLOCK_UPTIME_RAW` 经过时间”计算实际挂起时长，
   因此睡眠发生在状态请求或 exercise 内也不会漏记，普通慢请求则不会被误判为睡眠。
   如果 `pmset -g assertions` 显示 `PreventUserIdleSystemSleep`，不要等待闲置超时；应使用
   Apple 菜单“睡眠”或合盖后再唤醒，并确认 JSONL 记录了 `sleep-gap`。

```sh
./scripts/codex_pet_soak.command

# 等价的底层命令：
PYTHONPATH=scripts .venv/bin/python scripts/codex_pet_soak.py \
  --duration-hours 24 --sample-seconds 5 --exercise --exercise-seconds 600 \
  --require-sleep-gap --minimum-reconnects 2 --minimum-exercises 100 \
  --output ~/.vibeboard/codex_pet_soak_$(date +%Y%m%d_%H%M%S).jsonl
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
