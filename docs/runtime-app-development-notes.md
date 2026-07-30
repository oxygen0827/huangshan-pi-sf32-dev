# Runtime App 开发经验记录

这份文档专门记录黄山派 Runtime App 开发过程中踩过的坑、真实原因和后续复用规则。后续每开发一个新 App，如果遇到安装、启动、触摸、显示、资源、性能或 BLE 管理问题，都应该把结论补到这里，减少重复 debug。

## 记录格式

每个问题尽量按这个结构写：

- 日期 / App：什么时候、哪个 App。
- 现象：用户在板子或 Web 工具上看到什么。
- 容易误判的方向：最开始可能以为什么坏了。
- 真正原因：最后确认的问题归属。
- 修复方式：代码、协议或工具链怎么改。
- 后续规则：以后写同类 App 要怎么避免。
- 验证方式：用什么命令或真机操作确认。

## 2026-07-30：macOS 固件归档混入 AppleDouble 文件，发布哈希不稳定

### 现象

首次将签名固件 release 目录用 macOS `ditto` 打包后，ZIP 中出现 `._*` AppleDouble 文件。固件
本体没有变化，但归档 SHA-256 与无 sidecar 的归档不同，发布源中的哈希校验因此会拒绝客户端
下载的非同一归档。

### 真正原因

`ditto` 会保留 macOS 文件系统元数据。固件发布包是跨平台的不可变字节对象，不能把宿主机的
扩展属性或 Finder 资源叉带入其中。

### 修复方式

`scripts/firmware_release.py archive` 现在从已经验签的 release 目录按固定顺序和固定 ZIP
metadata 创建归档，并在写入前重新解压、验签。该命令拒绝现有目标文件和非 `.zip` 输出，也会
拒绝任何 `._*` 或 `.DS_Store` 条目。

### 后续规则与验证

所有上传到官方发布源的固件 ZIP 都必须由该命令产生。上传前记录其输出的 SHA-256，将该值写入
`codex-pet-companion/firmware-feed/releases.json`；上传后再通过 HTTPS 下载一次并比较相同哈希。

## 2026-07-30：单次 TLS EOF 被错误呈现为“官方固件发布源不可用”

### 现象

Companion 已成功读取过 `1.0.1` 发布源，但随后一次“检查固件更新”出现
`SSL: UNEXPECTED_EOF_WHILE_READING`，网页立即显示发布源不可用。服务器侧 Nginx 容器仍健康，
同一 TLS vhost 连续五次请求均返回 HTTP 200 且证书校验成功。

### 真正原因

固件源读取是幂等 GET，但 `_open_https` 把一次短暂的握手 EOF 当作最终业务失败，没有恢复机会。
这既不能说明发布源文件损坏，也不能说明板子或蓝牙有问题。

### 修复方式与后续规则

Companion 现在仅为 feed 和 release archive 的 HTTPS GET 增加最多三次有限重试与短退避。HTTP
错误、证书验证错误以及达到上限后的错误仍立即向用户报告，绝不通过禁用 TLS 验证来“修复”。每次
发布后应验证服务器本地 vhost 和真实 Companion 两条路径；一次 TLS EOF 必须先看重试后的结果，再
判断服务器故障。

## 2026-07-24：Codex Pet 显示已连接但运行状态不更新

### 日期 / App

2026-07-24，Codex Pet Companion / Desktop Monitor。

### 现象

Codex Desktop 中的任务正在持续思考并调用工具，但板端 Codex Pet 一直显示 connected，
任务数保持为 0，没有进入 running。Companion `/v1/status` 同时显示 `codex.bound=true`、
板子已连接，因此页面看起来没有故障。

### 容易误判的方向

这个现象很容易被误判为 BLE 丢包、`pet.tasks` 发送失败、板端 UI 定时器停止，或者宠物包的
running 帧与 idle 帧相同。仅检查“Bridge 进程存在”“蓝牙已连接”和“Hooks 已绑定”也会得到
假健康结果，因为这些检查都没有证明 Codex 实际允许运行 Hook。

### 真正原因

最后一个能正常产生 Hook 的 Desktop 会话使用 `Codex 0.146.0-alpha.3`；当前异常会话使用
`0.146.0-alpha.3.1`，版本切换发生在最后一次正常 Hook 和第一个异常任务之间。`hooks.json`
和 `codex_pet_hook.py` 在这个时间窗口内没有变化，但新版本启动 `/hooks` 时明确报告六个 Hook
为 new or changed。

通过当前版本 App Server 的官方 `hooks/list` 接口复核，六条 Companion Hook 的
`trustStatus` 全部是 `modified`。旧版本保存在 `config.toml [hooks.state]` 中的信任哈希不再匹配
新版本计算的 `currentHash`，所以 Codex 会静默跳过 `SessionStart / UserPromptSubmit /
PreToolUse / PostToolUse / PermissionRequest / Stop`。

旧 Companion 的 `bound=true` 只遍历 `hooks.json`，确认命令存在，不检查 Codex runtime 的
信任状态，因此把“配置存在”误报成了“状态链路可用”。Monitor 又完全依赖这些 Hook 更新任务
注册表，最终只能反复发布空任务快照。

诊断时向同一个 Unix Socket 注入合成 `UserPromptSubmit` 后，板端约一秒内从
`connected/tasks=0` 切到 `running/tasks=1`，指示灯变蓝、`uiTicks` 前进且
`droppedFlows=0`。这排除了 Monitor 后半段、BLE、板端状态机和硬件瓶颈。

### 修复方式

- Companion 使用 Codex App Server 的 `hooks/list` 读取每条 Hook 的 `enabled` 和
  `trustStatus`，不复制 Codex 内部哈希算法。
- `/v1/status` 保留 `codex.bound` 表示配置完整，新增 `codex.trusted`、
  `codex.trustStatus`、`codex.untrustedEvents`、`codex.trustError` 和结构化
  `codex.remediation`。
- 只有 `bound=true && trusted=true` 才把 Codex 显示为绿色并开放部署；`unknown` 也按未就绪
  处理，避免再次出现假健康。
- Companion 页面在 `modified / untrusted / disabled / incomplete` 时显示全宽项目告警，
  告知用户状态不会更新，并给出 `/hooks` 修复步骤。
- 探测结果缓存 60 秒；Hook 配置、Codex 配置或 Codex 可执行文件变化时立即重新检查，用户
  完成信任后无需等待整个缓存周期。

现场恢复步骤：

1. 在 Codex CLI 输入 `/hooks`。
2. 核对六条命令都指向当前仓库的 `scripts/codex_pet_hook.py --companion-managed`。
3. 选择 `Trust all and continue`。
4. 重启 Codex Desktop 或新建任务。

不要让 Companion 自动改写 `trusted_hash`，也不要把
`--dangerously-bypass-hook-trust` 当作日常启动参数。Hook 能在 sandbox 外执行，重新信任必须
保留为用户可见的安全确认。

### 后续规则

1. 健康检查必须分别表达 configured、authorized/trusted、observed 和 delivered，不能用一个
   `bound` 布尔值覆盖整条链路。
2. 依赖外部工具安全状态时，优先调用该工具的公开状态接口；不要复制私有哈希、缓存格式或
   版本相关实现。
3. 外部工具升级后，先检查 Hook/插件/MCP 的信任和启用状态，再排查 BLE 或硬件。
4. `unknown` 不能自动提升为 healthy。状态来源不可验证时，UI 应明确告警并关闭依赖该状态
   的危险或误导性动作。
5. 真机状态不更新时，用可自动清理的合成事件逐段验证：事件入口、Monitor 注册表、Bridge、
   BLE Flow、板端 reducer。在哪一段第一次不变，根因就优先位于那一段之前。
6. Companion 的“绑定”操作只负责写配置，不能替用户完成 Codex 的安全信任决定。

### 验证方式

离线回归：

```bash
.venv/bin/python scripts/codex_pet_companion.py --self-test
node scripts/codex_pet_web_test.js
PYTHONPYCACHEPREFIX=/private/tmp/huangshan-pycache \
  .venv/bin/python -m py_compile scripts/codex_pet_companion.py scripts/codex_pet_appserver.py
```

运行 Companion 后检查真实状态：

```bash
curl -s http://127.0.0.1:8790/v1/status
```

未处理事故现场时应看到 `bound=true`、`trusted=false`、`trustStatus=modified` 和六个
`untrustedEvents`；完成 `/hooks` 信任并重启任务后，应变为 `trusted=true`、
`trustStatus=trusted`。再启动一个 Codex 任务，板端应进入 running，任务结束后进入 ready。

## 2026-07-24：Petdex 九状态只预览未部署，且两帧动画不流畅

### 日期 / App

2026-07-24，Codex Pet / Petdex `.hpet` / Huangshan Pi 原生宠物 UI。

### 现象

Companion 图库已经能预览 Petdex 的九行动作，但部署到板子上的旧包仍只有五种任务状态，每种
固定两帧、180 ms 一帧。`Run Right / Run Left / Jumping / Review` 不会进入板子，长动作也被
裁成前两帧，实机看起来明显卡顿。

### 容易误判的方向

- 不能把源图行数等同于板端已部署状态数；旧转换器虽然读取九行 contract，仍只抽取五行。
- 不能通过把 `FRAMES_PER_STATE` 从 2 改到 8 后一次性全解压来解决。2B 的九状态共有
  `6/8/8/4/5/8/6/6/6`，合计 57 帧；160x173 RGB565A 每帧 83040 字节，全部常驻需要
  4733280 字节，超过当前 2.1 MiB 图像 PSRAM 池。
- 逐帧在 UI tick 中从 SD 读取也不可接受；会让 LVGL 卡顿，并与音频和 Runtime 安装争用同一
  TF 卡总线。
- 相邻帧做 RGB565 字节异或再 zlib 并不一定更小。2B 实测从 1.09 MiB 放大到 1.47 MiB，原因是
  移动区域的异或结果熵更高。

### 真正原因

旧 `VBPC v1` 的 header 固定声明 5 个状态、每状态 2 帧，板端结构也把十帧全部解压常驻。
`.hpet` manifest、Companion ready gate 和串口诊断又把 `frames=2`、`frameMs=180`、
`preloadedBytes=830400` 写死，因此网页九状态与物理板实际能力发生了分叉。

另一个限制是 Runtime 单文件安装上限 1 MiB。57 帧逐帧 zlib 的 2B `preload.bin` 为
1091073 字节，不能通过 BLE install blob 校验。仅提高 `.hpet` ZIP 上限不能解决板端单文件限制。

### 修复方式

- `VBPC v2` 固定九个 Petdex 状态，但每个状态记录独立的 `frameCount/offset/length`；帧数允许
  2..8，不再强制相同。
- 每个状态把全部 RGB565A 帧组成一个连续 `zlib-state-block`。板端切换状态时只做一次读取和
  一次解压，播放期间不访问 SD。
- 板端分配两个缓存槽，每槽最多 8 帧，共 1328640 字节。当前槽只供 LVGL 读取，后台
  `vbpetld` 线程在另一个槽持有共享 storage mutex 完成加载，GUI tick 收到完成序号后才切换。
- v2 使用 120 ms 帧周期，2B 保留全部 57 帧；透明像素统一清零，alpha 量化为 16 级，签名
  `.hpet` 中的完整 `preload.bin` 为 980403 字节，同时保留平滑透明边缘。
- 真机首次仍以一个 `preload.bin` 传输时，在 BLE bulk `seq=1283`、`offset=282480` 处表面断开。
  因此 Companion 在合成 Runtime 包时把已签名的 v2 文件拆成 124 字节目录和
  `state0.bin` 到 `state8.bin` 九个状态块；每个文件重新开始 transfer id、offset 和 sequence，
  同时绕过 Runtime 1 MiB 单文件上限。板端兼容拆分布局、v2 单文件和旧 v1 包。
- 分文件后继续使用四包突发窗口，真机仍会在累计约 105 个 BLE 包后进入半开 GATT 状态：能重连，
  但 `status` 只返回旧值，必须完整重启才能恢复。最小对照把窗口降为 1 后，真实前六个文件和
  abort 全部通过。因此 bulk v2 现在每帧都请求板端 ACK，用板端 mailbox/SD 已处理完成作为
  背压信号；吞吐略降，但不再让 CoreBluetooth 的“写入已接受”超前于板端消费能力。
- 完整服务复测又捕获到最后一帧成功 ACK 丢失：板端已经报告 `next=25 offset=5484`，重试时返回
  `rc=-7`（`-RT_EBUSY`）。主机现在仅在 transfer id、next、offset 全部精确命中且错误码恰为
  `-RT_EBUSY` 时按“已应用”继续；CRC、乱序、偏移或其他错误仍失败。这个规则让完成帧重试
  幂等，不以放宽完整性校验换成功率。
- 单用途探针的新 BLE 客户端稳定，而常驻 Companion 连接在先承载 heartbeat/任务 flow、再进入
  bulk 后仍容易丢通知。安装事务现在先置 `_installing` 暂停所有发布者，再关闭并新建一个加密
  GATT 客户端；这个专用会话完成传输、commit 和严格 ready gate 后才恢复快照同步。
- 连接恢复代码原先把 `disconnect_pause` 放在 CoreBluetooth `__aexit__` 之前，实际效果是先等
  0.8 秒、再断开、随后立即重连，板端来不及处理 disconnect event。顺序已改为停止 notify、
  实际断开、清理本地队列、等待冷却、再允许扫描重连，并用事件顺序测试锁定。
- “单连接累计约 103 个包就退化”后来被真机反证，它是通知 ACK 丢失造成的表象，不是连接寿命
  上限。曾尝试在 18 个文件之间主动断开，反而重复触发扫描、加密和 CCCD 订阅竞态；最终实现
  删除文件边界重连，只在真实控制/数据 ACK 失败时最多执行三轮冷却、重连和 Runtime 验证。
- 实机捕获到 `install_begin` 已确认，紧接着的 `install_bulk` 在 CoreBluetooth 侧“写入成功”却
  没有业务 ACK，只能反复读到旧 `install_begin`。把控制文本改成 Write Request 仍会复现，说明
  ATT 完成不是板端 worker 完成。最终协议让 `install_begin/install_bulk` 在 ACK 超时后重连并以
  相同参数最多重发三次；板端把同 staging path、size、transfer id 的重复 bulk begin 在任意已接收
  offset（包括已完成）都视为幂等成功，重复的同名 `install_begin` 也不再中止并重建 staging。
  其他 busy 冲突仍拒绝。`install_end` 保持提交结果不确定后的状态验证，不能盲目重发或 abort。
- 最终根因在板端通知发送：`sibles_write_value()` 使用有限 TX packet pool，队列满时会返回 0；旧
  代码忽略返回值，因此 worker 已写入 SD 并更新 `next/offset`，业务 ACK 却静默消失。固件现在
  检查返回值，以 5 ms 间隔最多重试 20 次，耗尽才打印 `status notify dropped`；通知先复制本地
  快照，避免共享 status 在重试期间被其他回调改写。
- 每次订阅 status characteristic 时，旧 CCCD 回调还会把共享状态改写为 `ok notify=1` 并主动
  通知，延迟到达后污染 `status/install_begin` 响应。最终固件只记录 CCCD 状态，不再生成这条
  非请求响应；主机仍会清空旧固件的 marker 以保持兼容。安装控制文本与 bulk 帧继续使用 Write
  Command，可靠性由幂等业务 ACK 和精确进度字段保证；普通非安装控制命令仍使用 Write Request。
- 监控器曾把完全相同的 `pet.tasks` 在每轮轮询中重复发布。失败现场在真正安装前已累计
  `flow=50` 和 `flow=91`，白白消耗 GATT 写入与通知预算，并更容易把常驻会话推入半开状态。
  `DeviceSession` 现在只在任务快照内容变化时发送；相同内容只刷新本地快照时间，首次连接和
  重连仍会重放最新值，heartbeat 继续独立承担连接保活。
- Petdex 源图临时不可达时，Companion 只对 `fetch failed` 启用本地缓存回退。缓存包必须重新
  通过 Ed25519 验签，slug 一致，且明确是九状态、120 ms 的 v2 包；转换、格式或签名错误仍
  直接失败，不能用旧缓存掩盖。
- 保留 `VBPC v1` 读取兼容。旧五状态包在九状态语义下回退为相近动作，但严格的新宠物部署
  gate 只接受 `preloadVersion=2` 和 `assetStates=9`。
- 状态映射为：idle -> `idle`，running/transcribing -> `running`，ready/recording -> `waving`，
  needs_input -> `waiting`，blocked -> `failed`，审批 -> `review`。左/右滑分别临时播放
  `runLeft/runRight`，点击宠物和部署后首次启动播放一轮 `jumping`，然后回到最新任务状态。
- `pet.preview` flow 可按名字锁定九种动作，供真机自动验收；发送 `auto` 恢复任务语义。状态
  JSON 新增 `assetState`、`requestedAssetState`、`assetStates` 和 `preloadVersion`。
- 2026-07-24 最终真机任务 `pet-d21912b1f01c52ce` 在单条加密 BLE 会话内完成 18 个文件、
  1009703 字节和 4621 个步骤，未发生重连或进度回退。ready gate 返回 `pet=nier-2b`、
  `preloadVersion=2`、`assetStates=9`、`frameMs=120`、`preloadedBytes=1328640`、
  `loaderPhase=0`、`droppedFlows=0`。

### 后续规则

1. 图库预览 contract、转换器、包清单、固件状态枚举和 ready gate 必须同时表达九状态；不能只
   改网页标签。
2. 动画内存预算按“单帧字节数 x 最大帧数 x 缓存槽数”计算，不按包的总帧数计算；播放函数
   必须保持零 SD I/O。
3. 可变帧格式必须把每状态帧数写入包目录和 manifest，不能用全局 `framesPerState` 猜测。
4. 压缩策略必须用真实复杂宠物测量，并同时检查 `.hpet` 总大小、Runtime 单文件上限和板端
   解压峰值；理论上相似不代表异或差分一定更小。
5. BLE write-without-response 只表示主机栈接受了写入，不表示板端 mailbox 和 SD 已消费。窗口
   大小必须由真机长传输决定；当前稳定基线是二进制 bulk window=1，未经完整包压力测试不能
   调大。事务控制文本必须等 Runtime status ACK，并且仅在板端具备明确幂等条件时才能重发。
6. 后台加载线程不能调用 LVGL。非活动缓存覆写前由 GUI 线程失效图像缓存，完成后按 sequence
   原子接管，旧请求完成时不得覆盖更新的状态请求。
7. “部署完成”必须验证九状态 v2、当前状态至少两帧、120 ms、resident cache 非零和
   `uiTicks` 前进；仅看到文件传输 100% 不算成功。

### 验证方式

离线与构建：

```bash
node scripts/build_hpet_petdex.js --self-test
.venv/bin/python scripts/hpet_package.py --self-test
.venv/bin/python scripts/runtime_install_serial.py --self-test
.venv/bin/python scripts/codex_pet_companion.py --self-test
.venv/bin/python scripts/runtime_architecture_audit.py --self-test
./scripts/build.sh
```

暂停 Companion 发布者后，在同一串口连接内运行（CH340 每次重新打开可能重启板子，不能把发送
和读取拆成两个进程）：

```bash
.venv/bin/python scripts/runtime_install_serial.py /dev/cu.usbserial-13220 \
  --codex-pet-sweep --ready-timeout 12 --no-echo
```

工具逐个发送 `idle/runRight/runLeft/waving/jumping/failed/waiting/running/review`，完成后发送 `auto`。
2B 真机帧数应为 `6/8/8/4/5/8/6/6/6`；每次要求 `requestedAssetState` 等于 `assetState`、
`frame` 前进、`loaderPhase=0`、`uiTicks` 增长且 `droppedFlows=0`。切换期间串口不得出现
`spi sem timeout`、zlib 解压错误、assert 或 hard fault；动画稳定播放时不得产生 SD 读取日志。

## 2026-07-05：2048 滑动不灵敏

### 日期 / App

2026-07-05，`game_2048`。

### 现象

2048 已经能显示彩色棋盘，也能通过 Web / BLE 启动，但在板子上上下左右滑动时不稳定，表现为“滑了没反应”或者“非常不灵敏”。同一时间，左边缘右滑返回桌面和 K1 返回桌面是可用的。

### 容易误判的方向

一开始容易怀疑是触摸硬件、FT6146 驱动或板子触摸链路不稳定。但左边缘右滑能稳定回桌面，说明底层触摸事件链路是通的，问题更可能在 App 自己的事件处理方式。

### 真正原因

2048 第一版只依赖 LVGL 的 `LV_EVENT_GESTURE` 来判断方向。这个做法在小屏手表类设备上不够稳，原因有三个：

1. `LV_EVENT_GESTURE` 有自己的判定阈值，小屏短距离滑动不一定会被 LVGL 识别成 gesture。
2. 2048 棋盘由 `board_panel`、tile 和 label 多层 LVGL 对象组成，触摸事件可能落在子对象上；如果子对象没有正确冒泡或绑定回调，根节点不一定能可靠收到完整的按下 / 松开链路。
3. 游戏滑动和系统返回手势都在同一个屏幕上，必须让左边缘右滑优先处理，否则会和游戏内左/右滑互相干扰。

### 修复方式

在 `src/gui_apps/VibeBoard_Runtime/main.c` 中做了这几类改动：

- 增加 `VB_2048_SWIPE_MIN_PRIMARY 16`，把游戏内滑动阈值降低到更适合圆形小屏的距离。
- 不再只等 `LV_EVENT_GESTURE`，而是在 `LV_EVENT_RELEASED` / `LV_EVENT_PRESS_LOST` / `LV_EVENT_CLICKED` 时，用按下点和松开点计算 `dx/dy`，再决定 `LV_DIR_LEFT` / `LV_DIR_RIGHT` / `LV_DIR_TOP` / `LV_DIR_BOTTOM`。
- 仍保留 `LV_EVENT_GESTURE` 作为辅助路径，但通过 `drag_consumed` 防止一次滑动触发两次移动。
- 把触摸回调直接绑定到 `game2048.board_panel`，并给棋盘设置 `LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK`。
- 给每个 tile 设置 `LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK`，给 label 设置事件 / 手势冒泡，避免触摸落在数字文字或格子上时丢事件。
- 系统返回手势仍然优先：如果按下点在左边缘，并且 `dx` 达到返回阈值，就先执行返回桌面，不进入 2048 移动逻辑。
- 最后没有在 `LV_EVENT_PRESSING` 阶段移动棋盘，避免手指刚开始拖动时就提前移动或重复移动。

### 后续规则

开发需要滑动、拖动、方向输入的 Runtime App 时，不要只依赖 `LV_EVENT_GESTURE`。推荐默认采用下面的策略：

1. 记录 `LV_EVENT_PRESSED` 的起点。
2. 在 `LV_EVENT_RELEASED` / `LV_EVENT_PRESS_LOST` / `LV_EVENT_CLICKED` 用 `dx/dy` 自己判断方向。
3. 只把 `LV_EVENT_GESTURE` 当作补充，而不是唯一输入来源。
4. 对实际可触摸区域的父容器绑定触摸回调，并让子对象打开 `LV_OBJ_FLAG_EVENT_BUBBLE` / `LV_OBJ_FLAG_GESTURE_BUBBLE`。
5. 对游戏类 App 使用 `PRESS_LOCK`，避免手指滑出当前 tile 后事件链断掉。
6. 系统级手势，比如左边缘右滑返回桌面，要在 App 手势之前处理。
7. 小屏手势阈值要低一些，先从 12 到 20 像素范围试；2048 当前验证值是 16 像素。

### 验证方式

构建和烧录：

```bash
./scripts/build.sh
./scripts/flash.sh /dev/cu.usbserial-13220 --confirm-boot
```

通过本地 Web / BLE 桥接启动 2048：

```bash
curl --noproxy '*' -sS -X POST http://127.0.0.1:8765/api/runtime/apps/game_2048/launch
curl --noproxy '*' -sS http://127.0.0.1:8765/api/runtime/apps
```

真机验证：

- 在棋盘区域分别向上、下、左、右滑动，棋盘应移动并合并。
- 屏幕底部状态会短暂显示 `Moved left`、`Moved right`、`Moved up` 或 `Moved down`。
- K1 能返回主屏。
- 左边缘右滑仍能返回桌面。

## 2026-07-05：2048 不应该用大量 Lua 对象硬拼

### 日期 / App

2026-07-05，`game_2048`。

### 现象

最早的 2048 版本安装成功后，板子底部只显示类似 `2048，得分 2048` 的文本，没有真正的棋盘或可玩逻辑。后来改成 Lua 创建多个 label / button 后，虽然能显示静态棋盘，但仍然不可玩，而且接近 Runtime 脚本对象池限制。

### 真正原因

这个问题不是安装失败，而是 App 实现方式不对：

- Lua App 只画了静态 UI，没有实现游戏状态、滑动、合并、随机生成 tile 等逻辑。
- 即使完整 Lua 语言可用，LVGL 对象池和回调模型仍受控，不适合一开始就用大量对象硬拼复杂游戏。
- 之前的 `VB_MAX_SCRIPT_OBJECTS` 是固定数组实现限制，不是架构上限；已经从 24 提升到 96，但这不是复杂游戏的最佳路径。

### 修复方式

把 2048 改成 Runtime 原生 helper：

```lua
vibe_2048_game("2048")
```

Lua 只负责声明“启动 2048 游戏”，真正的棋盘、颜色、滑动、合并和计分逻辑放在 Runtime C 层。

### 后续规则

- 简单状态展示、传感器面板、按钮和文本可以继续用 Lua helper。
- 高交互、高对象数量、需要动画或输入状态机的 App，优先做 Runtime 原生 helper，再由 Lua 调用。
- 增加脚本对象池上限可以改善一般 UI 能力，但不能替代合理的 Runtime 能力抽象。
- 新增 helper 后要同步更新：
  - `src/gui_apps/VibeBoard_Runtime/main.c`
  - `scripts/runtime_package.py`
  - `mobile/ios/VibeBoardBLE/Sources/VibeBoardBLE/RuntimePackage.swift`
  - 对应 `scripts/runtime_apps/<app_id>/main.lua`

### 验证方式

离线包校验：

```bash
python3 scripts/runtime_package.py --package-dir scripts/runtime_apps/game_2048 --app-id game_2048
python3 scripts/runtime_package.py --self-test
```

本地桥接服务如果已经在运行，新增 helper 后要重启服务，否则旧进程会继续使用旧的 helper 白名单，导致安装时报：

```text
unsupported Runtime Lua helper 'vibe_2048_game'
```


## 2026-07-08：Web App Manager 启动 App 后屏幕闪烁并自动回桌面

### 日期 / App

2026-07-08，`auto_snake` / Web App Manager / RuntimeTransport serial bridge。

### 现象

在本地 App Store 的“板上 App Manager”里点击 `Auto Snake` 的“启动”后，黄山派屏幕会连续闪烁很多次，随后才短暂进入 App；有时启动一两秒后又自动回到桌面。停止本地 App Store 服务后，板子不再闪烁。

### 容易误判的方向

容易以为是 `auto_snake` App 逻辑坏了、Runtime 启动慢、LVGL 切屏崩溃，或者新板子硬件不稳定。但停止浏览器桥接服务后现象立即消失，说明问题重点在主机侧桥接服务对串口的访问节奏，而不是 App 本身。

### 真正原因

Web bridge 在发送 `launch` 后，马上连续刷新 transport status 和 Runtime app list。串口读取会让板端在 LVGL 切屏、Runtime reload 和日志输出期间被额外命令打断，表现为屏幕反复刷新，严重时还会触发回到 Home 的路径。

另外，Runtime 的 `stop/home` 路径曾经只停止 UI，没有同步把 `/sdcard/apps/.active` 清回 `welcome`，导致网页和板端可能看到旧 active app，增加误判。

### 修复方式

- `scripts/app_store_server.py` 去掉周期性 `refreshTransport` 轮询。
- Web 页面启动、停止、删除、安装完成后，先用本地缓存更新 App 列表和 active 状态，不再立刻打串口读取 `status/apps`。
- `pollJob(...)` 返回最终 job，只有安装任务真正 `done` 才把 App 乐观加入板上列表；失败不再假装已安装。
- `src/gui_apps/VibeBoard_Runtime/main.c` 的 Home/Stop 路径会把 `.active` 写回 `welcome`，并更新内存里的 `active_app`。
- 本地 App Store 状态栏只显示摘要，不再把整段串口日志显示到页面右上角。

### 后续规则

1. 对串口 transport，Web UI 不要做后台轮询，尤其不能在 `launch` 后立即刷新 `status/apps`。
2. App 启动、停止、删除、安装完成后，优先用命令结果和本地缓存更新网页；需要真实状态时让用户手动点“刷新”。
3. 桥接服务必须有 transport lock，所有 board-facing 命令串行执行。
4. 安装任务失败时，不要更新“已安装 App”缓存。
5. Home/Stop 必须同步清理 `.active`，否则下次启动和网页状态会被旧 active 误导。

### 验证方式

```bash
scripts/app_store_server.py --self-test
PYTHONPYCACHEPREFIX=/private/tmp/huangshan-pycache /usr/bin/python3 -m py_compile scripts/app_store_server.py scripts/runtime_transport.py scripts/runtime_package.py
/usr/bin/python3 scripts/runtime_architecture_audit.py --self-test
/usr/bin/python3 scripts/runtime_deep_check.py --self-test
./scripts/build.sh
```

真机验证：启动本地 App Store，点击 `auto_snake` 的启动按钮，板子应直接进入 App，不应连续闪烁，也不应在一两秒后自动回到桌面。需要再次确认板上列表时，手动点网页“刷新”。

## 2026-07-08：圆角屏安全区和桌面卡片布局

### 日期 / App

2026-07-08，板端首页 / Runtime App UI。

### 现象

黄山派屏幕物理可视区域边缘是圆弧，不是完整直角矩形。标题、按钮或右上角 Home 键贴近 390x450 几何边界时，真机上会被外壳和圆角遮挡；例如 App Manager 顶部按钮、Auto Snake 右上角 Home 按钮曾经不可完整看到。

### 容易误判的方向

模拟器或代码里看 390x450 画布是完整矩形，容易误以为只是字体过大或按钮位置偶然不准。真机照片表明这是产品外观和显示窗口共同造成的安全区问题。

### 真正原因

LVGL 坐标系仍是矩形，但用户能看到、能稳定点击的区域更接近带圆角/弧边的安全区。把导航按钮、标题、状态栏或列表行贴到边缘，会被圆弧区域裁掉。

### 修复方式

- 主屏使用 `HUANGSHAN_HOME_SAFE_LEFT/RIGHT/TOP/BOTTOM`，把标题、状态和卡片列表放进安全区。
- Runtime 内部页面使用 `VB_SCREEN_SAFE_LEFT/RIGHT/TOP/BOTTOM` 和 `VB_SCREEN_SAFE_WIDTH/HEIGHT`。
- 板端首页直接显示已安装 App 卡片并支持滚动，不再要求用户进入单独 App Manager 页面。
- App 页面不再依赖右上角 Home 键；K1 是可靠的返回桌面入口，左边缘右滑作为触摸返回补充。

### 后续规则

1. 新页面不要把重要文字或按钮放在屏幕四角。
2. 优先使用统一安全区常量布局；确实需要全屏背景时，也要把可读/可点击内容放回安全区。
3. 手表式导航优先使用硬件 K1 返回桌面，触摸返回可做左边缘右滑，不要在右上角放小 Home 按钮作为唯一返回路径。
4. 列表和卡片要支持滚动，不要假设屏幕只能显示固定数量 App。
5. 真机照片或视频是布局验收依据；仅看截图/坐标不够。

### 验证方式

构建烧录后观察真机：主屏标题、卡片、状态行都应完整显示在圆角安全区内；安装超过一屏数量的 App 后可以上下滚动；打开 `game_2048`、`auto_snake` 等 App 后，K1 能返回桌面，核心内容不被边缘遮挡。

## 2026-07-11：完整 Lua 语言与受控 host binding 分层

Runtime 使用 Lua 5.5 执行完整语言语法，不再由主机校验器拒绝函数、循环、条件、
表或 App 本地模块。安全边界放在 VM 和 host binding：384 KiB 内存、50 万指令、
64 KiB 单脚本、App 目录文件沙箱，并且不开放 `os/io/debug/package` 和动态 C 模块。

音频通过 `vibe_audio_*` 和 transport `playback*` 暴露高层 PCM WAV 播放；App 只能
播放自身包内资源，不能直接访问 I2S、codec 或任意文件。`audio_stage` 同时覆盖了
Lua 函数/表/循环和 WAV 播放，是这一边界的最小回归包。

后续规则：完整 Lua VM 只描述语言能力，不代表完整标准库、完整 LVGL binding 或
无限资源；Python、Swift、板端 helper 与 capability JSON 必须同步；新音频格式需先
增加解析和坏文件测试；App 停止或 Lua 启动失败时必须停止音频 worker。

## 2026-07-20：长期 Bridge 不能依赖临时命令会话的日志 pipe

### 日期 / App

2026-07-20，Codex Companion / Codex Pet Monitor / BLE Bridge。

### 现象

板端显示未连接，但串口仍能确认 Runtime 和 `codex_pet` 正常运行，电脑上也能看到 Bridge
进程。IPC 请求最终收到 `internal_error:BrokenPipeError`，而不是板端状态。

### 真正原因

Bridge 被直接留在临时 Codex 命令执行会话中。父会话结束后 Python 进程变成无监督孤儿，
stdout/stderr pipe 的读取端已经消失。BLE 重连代码在真正连接前输出日志，日志 flush 抛出的
`BrokenPipeError` 阻止了每一次重连；错误处理再次写坏 stderr，又把协议 ACK 变成裸错误 JSON。

这不是板子、宠物资源、BLE 地址或 GATT 缓存故障。完整因果链和现场证据见
[Codex Pet Bridge 事故复盘](codex-pet-bridge.md#2026-07-20-事故复盘bridge-进程存在但板端显示未连接)。

### 后续规则

1. 长期 Bridge 只能通过正式 monitor 启动器或明确的系统 supervisor 运行，不能遗留在临时
   agent shell、一次性 PTY 或工具执行会话中。
2. 日志输出永远不能参与连接、心跳、任务同步或 IPC 的成功条件；关闭的 stdout/stderr 必须
   被降级为“丢日志”，不能让控制路径失败。
3. IPC 已解析请求后的错误必须继续使用协议 ACK；不要用另一套裸 JSON 错误格式掩盖根因。
4. `ps` 看到进程不代表服务健康。至少同时验证进程父子关系、socket、`connected=1` 和
   `uiTicks` 前进。
5. 当串口显示 `running=1` 而 Companion 未连接时，先查电脑端 Bridge 生命周期和日志 FD，
   不要先重刷固件。

### 验证方式

Bridge 与 MCP 自测必须覆盖坏日志 pipe 和协议内拒绝 ACK；真机使用一轮 storage stress
复现脚本验证 `passed=true`、0 新增丢包和 UI 心跳前进。正常 monitor 进程结构应为
`zsh monitor.command -> Python Bridge + tee`。

## 2026-07-22：Codex Companion 宠物、审批和启动稳定性专题

### 日期 / App

2026-07-22，`codex_pet` / Codex Companion / Petdex 宠物导入 / Runtime 启动链路。

本轮不是单个 UI 文案修改，而是从“宠物看起来不动”一路排查到板端任务状态、BLE Bridge、
SD/FAT 访问、Runtime reload 和真实固件启动的完整链路。后续遇到同类问题，应按下面的
边界逐层定位，不要把主机状态、传输状态、板端状态和 LVGL 画面混成一个问题。

### 问题一：宠物的待机和运行动作相同，运行时还有明显缩放

#### 现象

宠物在 `running` 时看起来和等待时一样，部分版本用缩放或放大缩小制造“运动感”，真机上
缩放非常明显且不自然。宠物位置、大小和状态切换也会因为图片或对象重新布局而抖动。

#### 容易误判的方向

容易先调 LVGL 的动画速度、缩放比例或定时器频率，以为“帧率不够”就是根因。实际上，
如果所有状态共用同一张图，调任何动画参数都不能产生真正不同的待机、运行、完成或错误动作。

#### 真正原因

状态机只有文字状态变化，但宠物资源没有按状态提供独立帧；旧的视觉补偿又通过改变对象
缩放实现运动，导致尺寸变化比动作本身更显眼。

#### 修复方式

- 宠物资源按 `idle`、`running`、`listening`、`needs_input`、`done/error` 等状态预载，
  每个状态使用自己的帧组。
- 每个状态至少保留两帧，运行时只切换预载帧，不再通过缩放制造动作。
- 固定宠物图片对象的位置、宽高和基准缩放；状态切换不能重新计算布局。
- 帧切换由固定的 native timer 驱动，当前验证帧间隔为约 180 ms。
- 缺少自定义宠物资源时才使用 Rocky/内置占位绘制，不让自定义资源路径回退到随机缩放。

#### 后续规则

1. “有动画”必须先证明每个状态有独立资源，再调帧率；禁止把缩放当作 running 动作。
2. 固定格式图片要在对象创建时确定稳定的尺寸和坐标，不能在每帧改变 LVGL 对象几何属性。
3. 新宠物导入时必须检查所有状态是否存在、帧数是否大于 0，并在板端日志确认实际预载数量。
4. UI 验收至少分别观察 idle、running、needs_input、done 和 error，不只看默认待机画面。

#### 验证方式

```bash
node scripts/import_petdex_pets.js --check-config
python3 scripts/runtime_package.py --all
./scripts/build.sh
./scripts/flash.sh /dev/cu.usbserial-13220
```

真机日志应包含类似：

```text
[vb_runtime][codex_pet] preloaded pets=1 states=5 frames=2 ...
```

之后分别触发任务运行、完成和等待输入；宠物应切换状态帧，尺寸保持稳定。

### 问题二：运行时读取 SD/FAT 导致屏幕卡死

#### 现象

宠物运行或切换动作时屏幕停止刷新、板子像卡死；预存多个宠物或在运行过程中读取宠物
图片时更容易出现。只上传一个宠物可以降低资源量，但单纯把数量改成 1 不能解决“每帧读 SD”
这个访问模型问题。

#### 容易误判的方向

容易怀疑是图片损坏、LVGL 解码器、BLE 连接或 PSRAM 不足。真正需要先区分的是：卡死发生
在资源安装阶段，还是发生在 GUI 渲染线程反复读 `/sdcard` 阶段。

#### 真正原因

LCD/AMOLED 刷新、动画和 SD/FAT 访问共享板端资源与调度路径。运行时在渲染或状态切换
期间同步读取图片，会阻塞 GUI；多个宠物还会增加查找、解码和内存压力。板端 Codex Pet
任务快照也不应该在 BLE 事件线程写 FAT 文件。

#### 修复方式

- 自定义宠物只允许一个 active 宠物，减少包大小、索引复杂度和 PSRAM 占用。
- 启动阶段一次性把 active 宠物的五个状态帧预载到 PSRAM；运行态只访问 RAM 中的
  `lv_img_dsc_t`，不再读 SD/FAT。
- 音效也在启动阶段预载；任务、heartbeat、选择状态只保留在 RAM，由电脑端 Bridge 的
  durable journal 负责跨重启恢复。
- `preload.bin` 是桌面/安装阶段生成的运行资源；不要把状态 `.bin` 当成每帧运行时文件。
- 安装使用 staging 目录，完整包校验成功后再提交，避免半包覆盖当前可运行版本。

#### 后续规则

1. GUI 定时器和 BLE 事件回调中禁止同步读 SD/FAT、解码大图片或写持久化日志。
2. “只保留一个宠物”是资源控制措施；真正防卡死的规则是启动预载、运行只读 RAM。
3. 新增图片或音效时必须报告解压后字节数、PSRAM 用量和启动时间，不能只看压缩包大小。
4. 出现卡死时先检查是否有运行态 SD 访问，再检查 LVGL 对象生命周期，最后才怀疑屏幕硬件。

#### 验证方式

检查包和启动日志：

```bash
python3 scripts/runtime_package.py --package-dir scripts/runtime_apps/codex_pet --app-id codex_pet
python3 scripts/runtime_package.py --all
.venv/bin/python scripts/runtime_install_serial.py /dev/cu.usbserial-13220 --status-only
```

日志必须显示 `fs=ready`、`preloaded pets=1 states=5` 和 Lua app started；运行多轮状态切换
后不得出现 `SPI timeout`、`hard fault` 或新的 FAT 读取错误。

### 问题三：Petdex 导入后资源、状态和板端显示不一致

#### 现象

从 Petdex 导入 Shinchan 等新宠物后，电脑端看似已有资源，板端却可能仍显示旧宠物、没有
运行帧，或者导入后屏幕不更新。

#### 真正原因

Petdex 页面资源、仓库导入配置、Runtime 包中的 catalog/preload 和板端 `.active` 是四个
不同层次。只下载网页图片或只改 JSON，不会自动更新板端 active 包；资源缺少某个状态时，
运行态又可能静默回退到占位图。

#### 修复方式

- 导入器先验证源帧，再生成 `scripts/petdex_pets.json`、catalog 和 preload 资源。
- Petdex 标准前九行固定为 `Idle / Run Right / Run Left / Waving / Jumping / Failed /
  Waiting / Running / Review`。图库必须完整展示九种动作，不能把前五行直接重命名成业务状态。
- 板端五个任务状态必须从九行动作中按语义抽取：`idle=0`、`ready/waving=3`、
  `blocked/failed=5`、`needs/waiting=6`、`running=7`。
- `scripts/petdex_state_contract.json` 是唯一映射源；Companion、`.hpet` 构建器和批量导入器
  必须共同读取它，不能复制常量。
- 当前产品策略明确为单一 active 宠物；导入新宠物时替换 active 资源，而不是让板端同时
  预存无限宠物。
- Runtime 包校验器检查 manifest、路径、状态帧、包大小和目录安全性；安装后由串口/BLE
  传输层逐文件发送并在 staging 提交。
- 板端启动日志报告实际预载的宠物数、状态数、帧数、原始字节和压缩字节，不能只看安装
  命令返回成功。

#### 后续规则

1. 新宠物验收顺序固定为：源站帧校验 -> Runtime 包校验 -> 安装 -> `.active` -> 冷启动日志
   -> 五状态真机观察。
2. 任何状态帧缺失都应在导入或包校验阶段失败，不要等到真机运行时才回退；禁止另写一套
   `0/1/2/3/4` 映射覆盖导入器已经验证过的 Petdex 行约定。
3. 记录当前 active 宠物名称和包校验结果，避免“电脑端喜欢的宠物”和“板端实际运行的宠物”
   混淆。

#### 验证方式

```bash
node scripts/import_petdex_pets.js --check-config
node scripts/extract_codex_rocky.js --check
python3 scripts/runtime_package.py --all
```

网页逐个确认九种 Petdex 动作；板端确认 `active=codex_pet`、`preloaded pets=1`，并逐个触发
idle/running/done/needs/error。

### 问题四：任务数量、`1 active` 和宠物动画没有随 Codex 任务更新

#### 现象

电脑上已经有任务在执行，但板端仍显示没有活动任务，宠物没有进入 running 动作，或者任务
记录一直累积导致“到底保留哪三个任务”不清楚。

#### 真正原因

任务状态经过 Codex Hook、桌面 Monitor、Bridge durable journal、BLE `pet.tasks` 和板端
渲染多个层次；任一层没有收到事件、选中项没有更新、旧 soak 任务没有清理，都会造成“连接了
但屏幕没变”的错觉。任务数量中的 `active`、`recent`、`i/n` 也不是同一个概念。

#### 修复方式

- Bridge 只维护真实 Codex 任务；`soak-*` 压测任务在 `Stop` 后立即删除。
- 快照明确区分 `ac`（活动任务数）、`n`（保留任务总数）、`i/n`（当前选中项）和任务状态。
- 任务 Hook 按单调序号去重，Bridge 重启从 durable journal 恢复；板端任务快照只在 RAM 中
  更新，不在 BLE 线程写 SD。
- `PreToolUse`、`PostToolUse`、`Stop` 等生命周期事件会清掉已处理的旧审批，避免任务看似
  一直卡在等待状态。
- UI 在收到运行态快照时切换到 running 帧；没有有效 heartbeat 时才进入 reconnect/offline，
  不把一次串口查询失败直接当成任务停止。

#### 后续规则

1. 看到 `1 active | 0 recent | 1/1` 时，`1/1` 是当前选中项/总数，不是“保留了几个隐藏任务”。
2. 验证任务同步必须同时检查电脑端 Hook 日志、Bridge snapshot、板端 `pet.tasks` 和 UI，
   不能只看桌面 Codex 窗口。
3. 压测、重启和真实任务必须使用不同 session ID；压测任务不得污染用户任务记录。

#### 验证方式

```bash
python3 scripts/codex_pet_hook.py --self-test
python3 scripts/codex_pet_monitor.py --self-test
python3 scripts/codex_pet_soak.py --self-test
python3 scripts/codex_pet_status.py --self-test
```

真机验证时提交一个长任务，观察 `active`、running 帧和任务详情；完成后确认进入 done，
再提交第二个任务并用左右键切换，最后检查压测任务不会留在 recent 列表。

### 问题五：自动审批显示 `Approval needed`，并和 `Approval required` 重复

#### 现象

Codex 使用“替我审批”时，板端同时显示黄色 `Approval needed` 和白色 `Approval required`。
用户看到的是两行重复文案，而且宠物会误进入等待审批状态。

#### 容易误判的方向

容易以为 Codex 的审批设置没有生效、BLE 把同一条消息发送了两次，或板端收到两个真实审批。
实际上，自动审批模式仍可能产生一个瞬时 `PermissionRequest` Hook；Hook 文案不是审批授权
结果，也不是可供板端点击的真实审批请求。

#### 真正原因

旧板端状态函数通过 `strstr(task_detail, "approval")` 推断审批状态，同时又把任务详情作为
第二行渲染。这样即使任务快照 `a=0`，也会把详情文字升级成 `Approval needed`，再显示原始
`Approval required`，形成重复。

#### 修复方式

- 真实审批唯一依据是任务快照 `a=1` 加有效一次性请求 ID `r`；详情文字不再改变审批状态。
- `a=0` 且详情为通用审批 Hook 时，Monitor 将快照归一为 `st=running`、`d=Approval handled`。
- 板端也有同样的兜底：无真实审批 ID 时保持运行态；通用审批详情隐藏，不渲染第二行。
- 真实审批时状态行是唯一来源，详情行隐藏，左右按钮才切换为 `Allow / Deny`。
- 审批成功、失败、超时和后续生命周期事件都会清理一次性请求，防止旧审批重新出现。

#### 后续规则

1. 永远不要用自然语言详情推断权限状态；必须使用结构化字段和有效请求 ID。
2. 自动审批 Hook 可以记录为诊断事件，但不能创建板端可操作审批。
3. UI 文案应有单一来源：真实审批只显示一次 `Approval required`，不要同时显示状态和详情。
4. 新增审批 Hook 或字段时，必须同时更新 Python Monitor、板端 C 解析器、协议自测和文档。

#### 验证方式

```bash
python3 scripts/codex_pet_monitor.py --self-test
python3 scripts/codex_pet_hook.py --self-test
python3 scripts/runtime_architecture_audit.py --self-test
```

必须分别验证：自动审批快照为 `a=0/st=running` 且无审批按钮；真实审批为 `a=1`、只有一行
`Approval required`、按钮为 Allow/Deny；后续 `PreToolUse`/`PostToolUse` 后 `a` 回到 0。

### 问题六：启动阶段重复拉起 `codex_pet` 导致 LVGL hard fault

#### 现象

最终实机验证时，显式再次执行 `--launch-app codex_pet`，板端出现：

```text
hard fault on thread: app_watc
DACCVIOL
MMAR:01000004
```

符号化地址落在 LVGL `lv_obj_get_parent` / `lv_obj_get_disp`，屏幕可能卡住或停在切屏动画。

#### 容易误判的方向

容易先归咎于 SD 图片读取、宠物 PNG 解码、BLE 传输或 LVGL 本身随机崩溃。地址中的无效对象
和日志中的两次 native pet start 表明这是对象生命周期和重复 reload 竞态。

#### 真正原因

Runtime 启动早期 `app_running` 还没有置位，但 active app 已经开始加载；此时再次 launch 会
被当成新的切换请求。旧 root 尚未完成切换动画和 Lua 初始化，就被第二次 reload/删除，后续
LVGL 访问悬空对象，最终触发 `DACCVIOL`。

#### 修复方式

- `vb_runtime_app_launch` 同时检查 `app_running`、`pending_reload` 和 `reload_in_progress`，
  同一 active app 的重复 launch 直接返回 already running。
- `vb_runtime_select_app` 和 `vb_runtime_request_reload` 对正在切换的请求做幂等处理；不同 App
  的请求合并成一次 pending reload，停止过程则返回 busy。
- `on_start` 在加载 active package 前置 `reload_in_progress=1`，完成后清零，覆盖最初的启动窗口。
- 保留 `lv_obj_del` 后立即清空 root 指针，避免后续逻辑继续使用旧对象。

#### 后续规则

1. App 的“已运行”“待切换”“正在 reload”“停止中”必须是不同的受保护状态，不能只看一个
   `app_running` 标志。
2. 启动、显式 launch、网页 launch 和 BLE launch 必须经过同一个幂等入口。
3. 任何 LVGL root 删除后马上置空；切屏期间禁止第二个命令修改同一个 root。
4. 看到 `hard fault` 时先用 ELF 符号化地址，再根据对象生命周期定位，不要只重复刷固件。

#### 验证方式

```bash
./scripts/build.sh
./scripts/flash.sh /dev/cu.usbserial-13220
python3 scripts/runtime_architecture_audit.py --self-test
```

冷启动后只采样一次 `vb_runtime_status`，确认 `active=codex_pet`、Lua app started、宠物帧
预载完成，日志中没有 `hard fault`、`DACCVIOL` 或 `SPI timeout`。不要在板子刚启动时连续
打开多个串口客户端反复发送 launch。

### 问题七：串口状态验证看起来重复启动，或误以为板子没有连接

#### 现象

执行 `runtime_install_serial.py --status-only` 后，输出可能包含两段相似的启动日志；有时
`app_manager` 还处于 `idle`，但后面已经出现 `lua app started`。如果只看输出开头，容易以为
板子重复启动、宠物被加载两次或 Codex 没有连接。

#### 真正原因

Huangshan Pi 的 USB-UART/CH340 打开串口可能触发 reset。`SerialTransport` 为了等待板端 ready
会先清理旧输出、轮询 `vb_runtime_status`，然后再发送实际命令；启动日志和 ready 探测输出
会混在同一次采样中。这个工具行为不能等同于应用进程被启动两次。

#### 修复方式

- 状态查询前等待 `connect_settle`，丢弃 reset 前的旧 Ready 响应，避免命令撞上启动阶段。
- 实机验证尽量只打开一个串口会话、只做一次只读采样；不要用多个命令并行探测板子。
- 以完整证据判断连接：`active`、`running=1`、Lua app started、BLE encryption/auth 或
  Runtime ready，而不是只看某一行 `app_manager=idle`。

#### 后续规则

1. `--status-only` 不是无副作用的纯日志读取；在 CH340 板子上要把它视为一次可能复位的
   连接操作。
2. 看到重复启动文本时，先检查命令是否在 ready gate 中重复发送 status，再判断是否有真实
   app launch 计数增加。
3. 实机验证完成后不要继续反复打开串口；避免验证动作本身制造新的 reset/竞态。

#### 验证方式

一次采样应至少看到：

```text
active=codex_pet
running=1
[vb_runtime][codex_pet] preloaded pets=1 states=5 ...
[vb_runtime][lua] lua app started: /sdcard/apps/codex_pet/main.lua ...
```

并且完整采样中没有 `hard fault`、`DACCVIOL`、`SPI timeout`。BLE 连接若同时出现
`encryption ... secure=1 auth=1`，说明电脑端连接链路已建立。

### 问题八：全量验证容易漏掉跨层回归

#### 现象

单独运行一个 Python 自测或只看板端画面时，可能漏掉 Python/Swift capability parity、Runtime
包目录、BLE/串口协议、Hook、MCP、语音和 iOS transport 的回归。审批修复后还曾在实机启动
阶段发现单元测试没有覆盖的重复 launch hard fault。

#### 真正原因

这个系统横跨桌面 Hook、Python Bridge、BLE/串口 transport、Runtime Lua/C、LVGL、SD/PSRAM
资源和 iOS 包校验；任何单一层的“通过”都不足以说明端到端可用。

#### 修复方式

- 用 `runtime_architecture_audit.py --self-test` 固化关键源代码契约，包括审批权威字段、
  重复启动保护、宠物预载和隐藏详情行。
- 用 `runtime_deep_check.py` 串起架构、协议、Bridge、Monitor、MCP、音频、Petdex、包语料、
  Python 编译、git whitespace 和 Swift 测试。
- 本轮最终代码在构建/刷写后重新完整执行三遍，而不是只对第一次修改执行三遍。
- UI 另跑 `huangshan-screen-ui` 审计；装饰性眼睛/尾巴的小尺寸警告与交互控件安全区问题
  分开判断，不能把 warning 误报成关键布局失败。

#### 后续规则

1. 修改跨层协议或板端 C 后，必须在最终代码、最终固件上重新跑三遍全量检查。
2. 每次真机验证至少包含：构建、刷写、冷启动、单次状态采样和关键错误日志扫描。
3. 测试输出中即使有 BLE mock 的预期拒绝日志，也要以命令最终 exit code 和 self-test 结果为准。
4. UI 审计 warning 必须注明是安全区风险、装饰元素还是实际交互控件，不能直接忽略全部 warning。

#### 验证方式

```bash
TMPDIR=/tmp python3 scripts/runtime_deep_check.py  # 连续执行三遍
sh .agents/skills/huangshan-screen-ui/scripts/audit-ui.sh \
  src/gui_apps/VibeBoard_Runtime/vb_runtime_codex_pet.c
git diff --check
```

本轮最终结果：三遍全量回归通过；固件构建和刷写成功；实机 `codex_pet`、BLE 安全连接、
五状态两帧预载和 Lua app 启动成功；未出现 `hard fault`、`DACCVIOL` 或 `SPI timeout`。

### 问题九：一键部署把“BLE 已连接”和“Codex Pet 已运行”混成一个状态

#### 现象

板子正在运行 `jump_jump` 等其他 App 时，BLE 已完成加密连接，但 Companion 网页仍显示
VibeBoard 未连接，部署按钮禁用。用户因此无法通过网页首次安装或修复 Codex Pet。

#### 真正原因

`DeviceSession._connect()` 在设置 `connected=true` 前调用 Codex Pet UI ready gate。这个 gate
要求 `active=1`、宠物帧存在并且 `uiTicks` 增长；其他 App active 时条件永远不成立。

#### 修复与规则

- Runtime service UUID、加密状态和 `status` 握手成功，就表示传输已连接，允许安装。
- Codex Pet ready 是 App 层状态，不能反向决定 BLE transport 是否连接。
- 普通连接只探测一次宠物状态；严格的 `slug/frames/frameMs/preloadedBytes/uiTicks` gate 只放在
  `install_end` 之后。
- 首次安装、宠物损坏修复和从其他 App 切换到 Codex Pet 必须列入回归用例。

### 问题十：BLE 安装误用串口 250 字符上限

> 2026-07-24 修订：本节的 500 chars / 200 bytes 是早期吞吐实验值，已被问题二十六的
> 255 chars / 96 bytes 实机稳定参数取代，不得再作为当前实现配置。

#### 现象

177620 字节的 Codex Pet Runtime 包即使请求 160 字节分块，最终仍生成 1912 条 BLE 命令，
传到中段后断线概率和总耗时明显增加。

#### 真正原因

串口和 BLE 共用 `build_install_commands()`，函数内部固定使用 FinSH 的 250 字符安全上限。
长路径扣除前缀后，160 字节请求实际只剩约 90 字节 payload。BLE 已协商 MTU 527，仍被串口
约束无谓切碎。

#### 修复与规则

- `build_install_commands()` 接收显式 `max_command_chars`；串口默认继续使用 250。
- BLE 使用 500 字符上限和 200 字节目标分块，实际最大命令 465 字节。
- 同一个 177620 字节包从 1912 条降为 895 条，仍低于板端 896 字节命令缓冲和协商 MTU。
- 包大小、命令数、最大命令长度都必须成为安装回归证据，不能只打印进度百分比。

### 问题十四：Codex Pet 大包安装被固定 deadline 截断、网页预览闪烁

> 2026-07-24 修订：动态 deadline 结论仍有效，但 511 chars / 224 bytes 已被问题二十六的
> 255 chars / 96 bytes 取代；大 MTU 不代表持续 SD/SPI 写入时可以安全使用接近 512 字节的命令。

#### 现象

- 网页宠物预览播放到 spritesheet 的透明列时，用户看到“显示一帧、空白几帧、再显示”的闪烁。
- 部署 002 等较大的宠物时，传输到中段或末段出现 `Runtime transport method 'install_package' exceeded its deadline`，
  失败后板端保留旧宠物，网页误以为是 BLE 主动断开。

#### 真正原因

`.hpet` 部署还要合并可信 Codex Pet 基础资源。当前 002 包实际包含约 244 KiB 运行时文件，旧的
200 字节目标分片和 500 字符命令上限生成 1161 条 BLE 命令；180 秒是固定总 deadline，不能覆盖
BLE GATT 写入、板端 SD 写入和每条 ACK 的累积延迟。网页预览则无条件播放 8 列，未过滤透明空帧。

#### 修复与规则

- Companion 请求 224 字节分片，构建器按 511 字符命令上限自动夹紧；命令末尾换行后不超过
  CoreBluetooth write-with-response 的 512 字节 payload 上限。实测 002 命令数从 1161 降为约 1040，
  不再发送可能触发 GATT 长写断开的 545 字节命令。
- `install_package` deadline 按实际 payload 分片数动态计算，最低 360 秒、上限 900 秒；安装期间仍由
  单一 transport worker 串行发送，心跳不插入安装队列。超时只在动态预算耗尽后触发，不能用固定 180 秒截断大包。
- 网页加载 spritesheet 后用 Canvas 检测每个状态行的非透明列，只循环可见帧；状态切换重置帧索引，
  用 `requestAnimationFrame` 驱动连续动画。检测失败时回退到第 0 帧，不显示透明列。
- 回归必须记录：slug、运行时文件总字节、命令数、最大命令长度、动态 deadline、
  `preloadVersion/assetStates/frames/frameMs/preloadedBytes`，并在真实板子上连续部署至少 10 次后
  再发布。v2 包要求九状态和 120 ms，不再沿用旧 v1 的固定两帧数值。

### 问题十一：临时 CCCD 值会遮住真正的 BLE status 通知

#### 现象

重连时 Mac 有时先收到单字节 `0x01`，随后板端串口已经打印完整 `ok status ...`，但主机仍
不断读取 `0x01` 并最终超时。

#### 真正原因

第一次 `command()` 返回的通知不匹配时，`read_matching()` 后续只读 characteristic，没有先
消费 status notification queue。CCCD/缓存值先到达时，稍后真正的命令 ACK 被留在队列中。

#### 修复与规则

- `read_status_retrying()` 先消费已经排队的通知，没有通知时才读取 characteristic。
- `verify_connection` 失败后下一轮强制 fresh scan，不继续盲用缓存的 peripheral 对象。
- CoreBluetooth 完整发现 GATT，再按 UUID 选服务和 characteristic，避免局部缓存映射。
- 错误日志保留经过截断和单行清洗的异常详情，不能只打印 `RuntimeTransportError` 类型名。

### 问题十二：首次 bond/GATT 服务变更窗口比普通重连长

#### 现象

板端首次配对或固件改变 GATT 表后，安全请求、Service Changed 注册、MTU 交换可能持续约
30 秒。Bridge 只等待 12 秒，每次都提前断开，形成永不成功的连接循环。板端清除 bond 而
Mac 仍保留记录时，CoreBluetooth 明确返回 `Peer removed pairing information`。

#### 修复与规则

- 首次 `verify_connection` 等待 45 秒，transport 队列使用 BLE 实现提供的动态上限；当前首次
  配对可覆盖两轮完整客户端重建，稳定重连仍按实际完成时间立即返回。
- “Peer removed pairing information” 不能靠无限重试修复，必须在板端和 macOS 两侧删除
  旧 bond 后重新系统配对。
- 配对 UI 要显示可操作错误；生产 Companion 只删除用户明确选择的 VibeBoard 记录，不能
  清空所有蓝牙设备。
- 固件升级涉及 GATT 表时，要测试旧 bond、单侧删 bond、双侧删 bond和首次配对四条路径。

### 问题十三：安装断线后的 abort 顺序错误

#### 现象

长传输断线后，底层立即在已失效连接上发送 abort，返回 service discovery/disconnected；
staging 只能等重启清理，网页也只得到一个泛化的 disconnected。

#### 修复与规则

- `DeviceSession` 先关闭失效 transport，再最多三次等待、重连并发送 `install_abort`。
- 安装期间继续归并 Hooks 快照，但不发送 BLE；恢复后只回放最后快照。
- 没有 `install_end` 时旧 App 必须仍可运行；提交后验证失败才使用缓存的上一宠物回滚。
- 故障注入必须覆盖 begin、file、end 和 ready gate，每个阶段都检查 active App 与 staging。

### 问题十四：测试桩的重复方法会悄悄覆盖新协议契约

#### 现象

生产连接增加 Runtime capability 强校验后，Bridge 自测在预期的日志/断线故障点之前失败；
MCP 用例还在断言旧的通用 `{ok:true}` 返回格式。

#### 真正原因

`FakeDeviceTransport` 同时定义了两个 `capabilities()`。Python 采用最后一次定义，后面的通用
hardware stub 静默覆盖了前面的正式 capability manifest，测试因此没有真正模拟生产协议。

#### 修复与规则

- 每个 transport 协议方法只保留一个测试实现；capability stub 同时记录调用并返回正式
  `api/rt/ble/ins.ble` 字段。
- MCP 集成测试直接断言 capability schema 和 BLE install 位，不能再用宽泛的 `ok` 代替。
- 新增连接 gate 后先检查 fake/adapter 是否完整实现 Protocol，再运行故障注入，避免测试在错误
  层级提前退出。
- 代码审查要搜索同一 class 中的重复 `def`，因为解释器不会对此给出警告。

### 问题十五：预览和生产签名密钥混用会造成验签假失败

#### 现象

离线预览实例生成的 Shinchan `.hpet` 能被 Companion 读回，但用默认生产公钥独立验证时报告
`invalid Ed25519 signature`。

#### 真正原因

预览服务把状态、缓存和 Ed25519 密钥隔离在临时目录；生产服务使用
`~/.vibeboard/companion/keys`。两个实例的 payload 完全相同，但签名和最终 ZIP digest 必然不同。

#### 修复与规则

- 验签必须显式使用生成该包的信任域公钥；预览密钥不能进入生产缓存，生产密钥不能复制到测试目录。
- 回归同时验证“正确公钥通过、其他实例公钥失败”，不能只验证成功路径。
- 日志和部署链接以整个 `.hpet` 的 digest 为准；不能假设相同 spritesheet 会跨签名实例得到相同 digest。
- 正式发布时由构建服务使用固定、受保护的发布密钥，并把对应公钥固定在签名、公证后的 Companion 中；
  本地自动生成密钥只用于当前开发/离线模式。

### 问题十六：深链参数在刷新时被重复当成新安装请求

#### 现象

页面 URL 保留 `?source=petdex&install=shinchan`。用户刷新后，搜索框再次自动填入
`shinchan`；当 Codex 和板子都已连接时，还会自动启动一次新的 Shinchan BLE 安装。

#### 真正原因

`boot()` 每次加载都把 `install` 参数复制到图库搜索和 `pendingInstall`，但从未清理地址栏，
也没有区分首次深链导航与普通刷新。一个本应一次性的安装意图因此变成可重复副作用。

#### 修复与规则

- 有效深链在首次导航时记录为内存安装意图，并立即用 `history.replaceState` 清除查询参数。
- `PerformanceNavigationTiming.type=reload` 时只清理旧参数，不搜索、不滚动、不部署。
- 深链部署不再依赖把整个图库筛选成目标 slug；搜索框始终属于用户自己的查询状态。
- `codex_pet_web_test.js` 固化“不写搜索框、识别 reload、消费后清 URL”三项契约，并接入
  `runtime_deep_check.py`。

### 问题十七：`install_end` 成功通知与应用重载发生线程竞态

#### 现象

宠物文件已传完，网页进度到 88% 至 90% 的“重启应用”后 BLE 断开，板子画面停止更新，
网页最终只显示 `disconnected`。重新读取板端时不能仅凭网页失败判断包是否提交，因为断线点
位于事务提交和最终 ACK 之间。

#### 真正原因

BLE 写回调先执行 `vb_runtime_install_end_app()`。旧实现提交 staging、写 `.active`、删除 backup
后立即把 LVGL reload timer 标成 ready；真正的 `ok install_end ...` 通知要等函数返回后才调用
`vb_ble_notify_status()`。GUI 线程可能先开始销毁旧 UI、读取 SD 并向 PSRAM 解压 830400 字节动画，
从而让最终 ACK 超时或链路断开。主机又把这个“提交结果不确定”当作普通文件传输失败，在失效
链路上发送 abort，既不能回滚已提交目录，也掩盖了真实状态。

#### 修复与规则

- 板端 `install_end` 只完成事务提交，并向唯一 BLE worker 投递延迟重载事件；worker 等待
  1500ms 后才请求 Runtime reload，让 GATT 回调先返回并发送 ACK。
- BLE transport 跟踪当前命令和 ACK 是否已收到。只有最终 `install_end` 尚未收到 ACK 时抛出
  `InstallCommitUncertain`，且绝不发送 abort；明确错误 ACK 和中途文件断线仍走原 abort 路径。
- Companion 遇到提交不确定时关闭半开 transport、重新扫描连接，并按目标 slug、2 帧、180ms、
  830400 字节预载、`queuedFlows=0` 和 `uiTicks` 递增做最终裁决。验证通过即判部署成功并只回放
  最新 Codex 快照；验证失败才交给上一宠物恢复逻辑。
- 网页安装期间禁用连接、解绑、部署和保存按钮，避免用户在 `_installing` 窗口再次发起配对。

#### 回归要求

故障桩必须模拟“板端已切换目标宠物，但最终 ACK 丢失”：底层不得出现 install abort，Companion
必须重连一次并返回目标宠物状态。真机还要确认 ACK 后才出现 reload、BLE 不掉线、画面恢复、
`uiTicks` 连续增长。

### 问题十八：刷固件后单侧 bond 遗失会让 CoreBluetooth 读错 GATT handle

#### 现象

板子仍以 `VibeBoard` 广播且 RSSI 正常，CoreBluetooth 也能建立链路和订阅，但第一条 `status`
带响应写入返回 `GATT Protocol Error: Invalid Attribute Value Length (13)`。改成诊断用无响应写后，
status characteristic 读回 `VBRTPEER`，即另一个服务的数据。

#### 真正原因

刷写后板端启动日志显示没有旧 bond，而 macOS 仍保存同一 peripheral UUID 的配对和 GATT handle
缓存。fresh scan 只能刷新 advertisement，不能清除 CoreBluetooth 内部的已配对服务表；继续扫描
或无限重连仍会按旧 handle 访问错误特征。这与安装中途的普通断线无关。

#### 修复与规则

- transport 将错误 13/`Invalid Attribute Value Length` 单独识别为 GATT 缓存失效，提示用户在
  macOS 蓝牙设置中忽略 `VibeBoard` 后重新连接，不再误报“BLE 不可用”。
- 开发刷写若没有保留板端 bond，必须同步删除 Mac 端该板记录；只能删除用户指定的 VibeBoard，
  不能清空所有蓝牙设备。
- 量产宠物安装不刷固件，因此正常的一键部署不应触发这条路径。固件升级流程必须保证 bond
  分区保留，或在 GATT schema 变化时提供 Service Changed/明确版本迁移。
- 诊断顺序固定为：确认 RSSI和广播、fresh scan、首条短 `status` 写入、返回 UUID/服务签名；
  看到错误服务数据时停止重试并重配对。

### 问题十九：后台心跳和网页配对会交叉执行两套 BLE 建连事务

#### 现象

网页点击连接后，日志在同一时间出现两条 `connect attempt`，约 61 秒后两条连接又几乎同时
超时。底层命令虽然经过同一个队列，但扫描、连接、服务发现和能力校验会被另一条协程的
`close()` 插入，导致单进程内也能互相拆掉连接。

#### 修复与规则

- `DeviceSession` 用连接级 `asyncio.Lock` 包住完整建连事务，而不是只串行化单条 transport 方法。
- 后到的连接请求在锁内看到已有可用会话时直接复用；连接失败标记和 close 也必须使用同一把锁。
- 心跳失败的清理在同一把锁内完成，不能在释放锁后再 `close()`；安装开始/结束时对
  `_installing` 的读写也必须与配对、重连共享该锁。
- 回归测试同时启动两条 `_connect()`，底层 `connect_count` 必须严格等于 1。
- 回归测试还要覆盖“旧心跳失败 + 新配对同时发生”，确认旧协程不会关闭新会话。
- “只有一个 BLE 进程”不等于“只有一条 BLE 事务”；所有会创建、关闭或替换 client 的路径都要
  纳入同一个生命周期锁。

### 问题二十：延迟重载仍从 BLE worker 直接操作 LVGL timer

#### 现象

`install_end` 延迟 1500ms 后仍会让板子停止广播和刷新，直到硬件复位才恢复；复位后 active
仍是旧 App，说明网页进度到“重启应用”不能证明新宠物已稳定启动。

#### 真正原因

延迟事件运行在 BLE worker，`vb_runtime_request_reload()` 却调用 `lv_timer_ready()`。LVGL timer
链表只能由 GUI 线程操作；BLE 线程与 GUI tick 并发修改时，应用删除、SD 读取和动画预加载尚未
开始就可能破坏调度状态。

#### 修复与规则

- worker 只设置 `pending_reload`；现有 200ms LVGL timer 在 GUI 线程消费标志并执行重载。
- 不再为了缩短最多 200ms 的等待而跨线程唤醒 LVGL timer。
- 架构审计直接检查 `vb_runtime_request_reload()` 实现，禁止重新引入 `lv_timer_ready()` 调用。
- 所有 BLE、shell、文件传输回调都只能发布 UI 意图，不能直接创建、删除或唤醒 LVGL 对象。

### 问题二十一：忘记设备后旧 CoreBluetooth UUID 会吃完整个连接超时

#### 现象

macOS 已忽略 VibeBoard，fresh scan 能看到同名新 peripheral，但 Companion 仍先使用缓存 UUID；
旧对象挂满 60 秒后才被命令队列取消，已有的“失败后扫描”代码根本没有机会执行。禁用缓存后
能连接服务，但首次订阅返回 `CBATTErrorDomain Code=15 Encryption is insufficient`。

#### 修复与规则

- 缓存或固定的 peripheral UUID 必须先通过实时广告解析，再建立连接；不要直接对旧 UUID 启动
  Bleak service discovery，也不要用短 `wait_for` 取消 CoreBluetooth 的半开服务发现。
- 后台回退扫描仍固定旧 CoreBluetooth 身份，不能自动接受附近同名板子；只有用户主动点击网页
  “连接板子”才允许重新绑定身份。
- 网页的“连接板子”操作显式要求下一次连接忽略旧缓存 UUID，并把新广告地址写回缓存；这样更换
  板子后不需要手动编辑本地 JSON，也不会继续反复尝试已经移除的旧配对。
- macOS CoreBluetooth 没有显式 `pair()`；首次配对必须先对受保护的 status characteristic 做读操作，
  触发系统自动配对，等待加密完成后才能写通知 CCCD。
- 错误 15、`Encryption is insufficient`、ATT 5/15 和配对期间的受保护读取超时都进入有界的
  45 秒认证窗口；连接总 deadline 会覆盖两轮 fresh scan、服务发现和认证（默认约 170 秒），
  足够覆盖首次 bond 和 Service Changed 窗口。超过窗口必须给出配对失败，不能无限重试。
- 自动配对测试要求 Mac 处于解锁状态；锁屏会让受保护读取等待系统授权，不能据此判断板端坏死。

### 问题二十二：打开 CH340 串口本身可能复位板子

#### 现象

为读取“卡死现场”而打开 `/dev/cu.usbserial-*` 后，日志从 `SFBL` 和完整启动流程重新开始，原本
需要保留的死锁现场被诊断动作覆盖。

#### 修复与规则

- Huangshan Pi 的 USB-UART 打开、DTR/RTS 初始化或监视脚本可能触发复位；串口采样必须标注
  是“现场被动日志”还是“复位后的恢复日志”。
- `monitor.sh` 是明确的复位加启动确认工具，不能用于证明复位前的 Runtime 状态。
- 安装故障应优先保留 Companion 阶段、ACK 和 BLE 广播证据；需要串口全程日志时，应在安装前
  打开并保持同一个串口会话，避免在失败后重新打开端口。

### 问题二十三：两个连接事件重复触发 BLE 安全请求，最终以 0x45 超时断开

#### 现象

首次清除 bond 后，Companion 能看到 `VibeBoard` 广播并建立链路，但传输尚未开始就返回
`BLE pairing did not complete before the authentication timeout`。板端随后打印：

```text
GAPC_PAIRING_FAILED 69
BLE_GAP_DISCONNECTED_IND, 19
```

SDK 中十进制 `69` 是 `GAP_ERR_TIMEOUT (0x45)`，表示 SMP 配对窗口超时，并非宠物包错误。

#### 真正原因

Runtime 同时订阅了底层 `BLE_GAP_CONNECTED_IND` 和 Connection Manager 的
`CONNECTION_MANAGER_CONNCTED_IND`。两条回调都调用 `connection_manager_set_link_security()`；
在同一连接上重复发送安全请求会让 CoreBluetooth 的受保护读取、Service Changed 和 bond 流程
互相覆盖。主机侧原本只有 20 秒窗口，也会在板端仍等待 SMP 时提前关闭连接并开始下一轮扫描。

#### 修复与规则

- 固件用 `security_requested[conn_idx]` 做每条连接的一次性门闩；两个事件只能有一个安全请求，
  另一个事件记录 `already_pending` 并复用同一链路。断开时清除门闩，允许下一条连接重新配对。
- 主机首次认证窗口调整为 45 秒；Bridge 按两轮扫描、建连、服务发现和认证动态计算约 170 秒
  的总连接 deadline，普通重连仍会在已有 bond 上快速完成。
- 架构审计要求安全请求只能从一个受保护 helper 发出，并检查 raw/manager 两个事件都经过该
  helper，防止后续改动重新引入直接调用。
- 真机验收必须看到每个 `conn_idx` 只有一条 `security requested ...`，随后出现 `secure=1`、
  `ok status` 和稳定的通知订阅；失败时不得在同一连接上继续发送第二条安全请求。

### 问题二十四：新板首次 provisioning 串口安装过慢，误判为蓝牙/板子卡死

#### 现象

新板刷完固件后，用普通 FinSH 文本命令安装内置 Codex Pet。基础包只有 9 个文件、172,785 bytes，
但默认 48-byte 原始分片会产生 3,605 条 ACK 命令；每条命令还要经过 24-byte UART 写入和响应等待，
首次安装可能持续十几分钟，用户容易在中途拔线或再次打开串口，最终把一次慢安装变成半包或复位。

#### 修复与规则

- provisioning 脚本在刷入包含 `vb_runtime_install_blob` 的当前 Runtime 后，默认使用
  `--binary-install`：每个文件按 3,072 raw bytes 编码、逐块 ACK，当前包约 72 个确认点，仍保留
  `install_begin/end/abort` 事务和最终 `active=codex_pet` 校验。
- 普通文本安装保留给旧固件兼容路径；不要把 `--chunk-bytes 240` 当成串口加速方案，因为 FinSH
  仍受 250 字符命令上限约束。
- 首次 provisioning 期间只保留一个串口会话，不并行启动 Companion 或第二个安装器；成功证据必须
  同时包含 `installed codex_pet`、`active=codex_pet`、`running=1` 和 `uiTicks` 持续增长。

### 问题二十五：CCCD 认证失败后在同一个 Bleak client 内重试永远不能恢复

#### 现象

受保护的 status 读取已经触发 macOS 配对，但第一次订阅通知仍可能返回
`GATT Protocol Error: Insufficient Encryption (15)`。代码原地重试 `start_notify()` 后，下一次固定返回
`Characteristic notifications already started`，网页最终显示连接或部署失败。

#### 真正原因

Bleak 的 CoreBluetooth delegate 会先登记 characteristic callback，再异步写 CCCD；CCCD 写入失败时
只清理等待 future，不会移除已经登记的 callback。同一个 client 已处于半订阅状态，不能再次调用
`start_notify()`。

#### 修复与规则

- 错误 15 后 best-effort `stop_notify()` 清理残留 callback，并立即废弃该 Bleak client；禁止在原
  client 内循环调用 `start_notify()`。
- 重新扫描刚才解析到的同一 CoreBluetooth UUID，再创建新 client、完成安全读取并订阅通知；不能
  因为附近有另一个同名设备就改变绑定目标。
- 动态连接 deadline 必须包含两轮完整扫描、建连、服务发现和认证，默认约 170 秒；正常已有 bond
  的连接仍会立即返回。
- 回归测试必须模拟第一个 client 在 `start_notify()` 返回真实错误 15、残留 callback 被清理、第二个
  client 成功订阅，不能只用一个没有 delegate 状态的假客户端。

## 2026-07-24：Codex Pet 大包 BLE 部署断链复盘

### 问题二十六：BLE 安装在约 `15/1007` 处断开，最终被判定为 deadline 超时

#### 现象

网页显示“蓝牙传输 · `nier-2b`”，进度停在约 `Transferring 15/1007`，随后 VibeBoard 变为
“等待蓝牙连接”，部署任务报：

```text
Runtime transport method 'install_package' exceeded its deadline
```

第一次只看网页时很容易把它归咎于 Mac 蓝牙、固定 peripheral UUID、Petdex 包损坏或用户拔线。
这些都不是根因。

#### 证据链

1. `.hpet` 摘要和签名校验成功，实际合成 Runtime 文件为 9 个、223453 bytes；失败发生在文件
   传输中，不是包校验阶段。
2. 旧配置使用 224 bytes payload，命令接近 500/511 字符，共 1007 条；`15/1007` 对应
   `assets/done.wav` 的早期块。
3. 在传输期间的板端串口出现：

   ```text
   spi sem timeout!
   spi(50095000) transfer errorB
   ```

4. 失败任务的状态、BLE 断开和 deadline 超时是同一条因果链：板端文件写入阻塞 BLE 事件处理，
   主机没有收到后续 ACK，最后才表现为“蓝牙断开”。

#### 真正原因

板端 `vb_ble_gatts_set_cbk()` 原来在 GATT 写回调中直接执行
`vb_ble_execute_line()`；`vb_runtime_install_file_chunk()` 随后同步访问 SD/FAT 和 SPI。这个
回调占用 BLE 事件线程的时间过长，屏幕刷新、SD 写入和 BLE ACK 互相争用 SPI/调度资源，最终
触发 SPI semaphore 超时和 BLE supervision deadline。

这不是把分片再缩小就能从根本上解决的问题。分片过大确实会增加 CoreBluetooth 回压，但只改
主机侧仍会留下“GATT 回调同步做文件 IO”的结构性风险。

#### 修复方式

- GATT 回调现在只组装完整命令、复制到堆内存并投递 `vb_ble` mailbox；它不执行文件系统操作，
  也不直接触发 Runtime reload。
- BLE worker 负责执行命令、发送逐块 ACK，并把 `install_end` 变成延迟的 pending reload；
  LVGL timer 仍由 GUI 线程消费，BLE 线程不能直接操作 LVGL 对象或 timer。
- worker 栈从 4096 提升到 8192 bytes。4 KB 版本在 `install_begin` 后出现板端重启；虽然现场
  没有留下可复现的 stack fault 文本，但重启只在这条深调用链出现、提升栈后消失，因此把 8 KB
  作为当前保守下限。后续不能把文件递归清理、路径拼接和 Runtime install handler 放回小栈的
  BLE 回调线程；若要下调栈，必须先加入 stack watermark 证据。
- BLE 安装单独使用 `max_command_chars=255` 和 96 bytes payload；串口仍保留自己的 FinSH
  250 字符限制，不能让两个 transport 共享一个“看起来安全”的上限。
- BLE 安装控制文本和二进制 bulk 帧都使用 Write Command，并严格等待 Runtime status ACK；控制
  命令靠有限重连重发和板端参数幂等，bulk 帧靠 transfer id、sequence、offset、CRC 和逐帧 ACK
  提供幂等与背压。普通非安装控制命令仍使用 Write Request。
- Companion 安装锁和 transport worker 保持单一 owner；安装期间不发送 heartbeat/task flow，
  成功后只回放最新快照。失败后的 `install_abort` 只能在明确未提交时执行，`install_end` ACK
  丢失必须走提交结果不确定的重连验证路径。
- 安装 deadline 必须按实际文件大小、分片数和每块 ACK 成本动态计算；固定的短 deadline 会把
  “慢但仍在推进”的安装误判为 BLE 断线。

#### 当前生效参数

| 层 | 参数 | 规则 |
| --- | ---: | --- |
| BLE 单条命令 | 255 chars | 为 CoreBluetooth 回压保留余量 |
| BLE 文件 payload | 96 bytes | 每块完成后等待板端 ACK |
| 串口 FinSH | 250 chars | 仅适用于 serial transport，不与 BLE 共用 |
| BLE worker 栈 | 8192 bytes | 覆盖 install、路径处理和 staging 清理调用链 |
| BLE 安装控制文本 | Write Command | 有限重连重发 + 参数幂等 + Runtime status ACK |
| BLE bulk 帧 | Write Command | transfer/sequence/offset/CRC + 逐帧 Runtime ACK |

旧文档中“BLE 224 bytes / 511 chars”的数值只能作为早期实验记录；本轮实机数据已经证明它在
持续文件写入下仍会掉链，后续实现和验收以本节参数为准。

#### 实机验证结果

修复固件重新构建并刷入后，完整安装任务 `pet-55211b6e54a4b1af` 从 36% 持续传输到
`Transferring 2217/2355`，随后进入提交、重启和完成状态：

```text
status=done
stage=complete
progress=100
message=2B is active
connected=1 state=running pet=nier-2b
frames=2 frameMs=180 preloadedBytes=830400
queuedFlows=0 droppedFlows=0
```

完整串口抓取中未再出现 `spi sem timeout`、`transfer error`、`hard fault` 或新的 BLE 断链；
安装已连续越过原来的 `15/1007` 断点，任务最终确认完整写入 `preload.bin`。

#### 后续排障规则

1. 先记录 job 的 `digest`、Runtime 文件总字节、命令总数和当前 `x/y`，再判断是否真的断链。
2. 必须把 Companion 日志、BLE ACK 和板端串口放在同一时间轴；网页 deadline 只是结果，不是根因。
3. 串口诊断要在安装开始前打开并保持同一会话；安装失败后才打开 CH340 可能复位板子，只能看到
   恢复日志，详见问题二十二。
4. 一次只允许一个 Companion/Bridge、一个 BLE owner 和一个安装 session；不要并行启动第二个
   安装器或第二个串口客户端。
5. 新改动必须同时跑 `runtime_transport`、`codex_pet_bridge`、Web 和架构审计自测，并做至少一轮
   真机完整安装；只看到“已连接”或进度条增长不能替代最终 `pet_status` 验证。

#### 回归命令

```bash
./scripts/build.sh
./scripts/flash.sh --port /dev/cu.usbserial-13220 --confirm-boot
.venv/bin/python scripts/runtime_transport.py --self-test
.venv/bin/python scripts/codex_pet_bridge.py --self-test
node scripts/codex_pet_web_test.js
.venv/bin/python scripts/runtime_architecture_audit.py --self-test
```

真机安装必须同时满足：任务 `status=done/progress=100`、目标宠物 `state=running`、
`preloadVersion=2`、`assetStates=9`、`frames>=2`、`frameMs=120`、`preloadedBytes>0`、
`queuedFlows=0`、`droppedFlows=0`，并且串口无 SPI/BLE 故障关键字。

以上一键部署实现和接口详见
[`codex-pet-one-click-deploy.md`](codex-pet-one-click-deploy.md)。

### 本轮可复用的总原则

```text
主机状态      -> Hook / Monitor / durable journal
传输状态      -> BLE/Serial ready、ACK、sequence、连接安全态
板端状态      -> pet.tasks 的 a/r/st 字段和 heartbeat
渲染状态      -> LVGL 固定对象 + PSRAM 预载帧
验证状态      -> 最终构建 + 最终刷写 + 三遍全量回归 + 一次实机采样
```

遇到“屏幕不变、宠物不动、显示未连接、审批重复或板子卡死”时，按这五层逐层取证；不要
仅凭某一行 UI 文案、某一个进程存在、某一次串口超时或 SD 卡文件存在就下结论。

## 2026-07-24：Codex Pet 主视觉放大与任务横滑

Codex Pet 普通状态不再常驻显示底部 `< / >` 任务按钮。宠物保持屏幕中线，缩放从 312
提高到 360，并下移到 `y=115`；160×173 帧的实际显示范围约为 `x=82..308`、
`y=80..323`，完整落在 Runtime 安全区内。

390×450 圆角屏的纵向预算如下：

```text
标题 / 连接状态   y=36..79    44 px
宠物主视觉        y=80..323  244 px
状态              y=324..345  22 px
任务详情          y=346..385  40 px
间隔              y=386..391   6 px
任务计数          y=392..411  20 px
安全区余量        y=412..413   2 px
```

任务切换改为屏幕中部横滑：左滑进入下一项，右滑返回上一项。实现同时处理
`LV_EVENT_GESTURE` 和按下/移动/松开的 `dx/dy`，28 px 横向阈值只触发一次；起点必须位于
中部安全区，左边缘不参与任务切换，继续保留 Runtime 的右滑返回桌面手势。

底部按钮对象只在真实一次性审批存在时临时显示为 `Allow / Deny`。普通任务浏览完全隐藏，
但审批仍使用明确按钮，不把有副作用的允许/拒绝动作绑定到容易误触的滑动手势。

## 2026-07-24：Codex Pet 点击跳跃在缓存命中后卡死

### 现象

宠物首次启动会正常播放一轮 `jumping`。稍后在屏幕上点击宠物时，图像进入跳跃状态，但可能
停在其中一帧，持续显示 `jumping`，不会自动回到当前任务对应状态；在跳跃过程中再次点击更容易
稳定复现。Companion、BLE 和 UI tick 此时仍然存活，因此这不是连接断开或 GUI 线程停摆。

### 根因

九状态实现使用两个 PSRAM 缓存槽。首次启动已经把 `jumping` 留在第二个缓存槽，后续点击会直接
命中缓存，不经过后台加载完成回调。旧代码只在异步加载完成回调里设置
`transient_started=1`，所以缓存命中的跳跃虽然被激活，却没有被标记为可结束；帧循环回卷时的
退出条件永远不成立。动画中再次点击还会重置瞬态状态，但同状态渲染不会重新激活缓存，进一步
放大这个问题。

### 修复

- `vb_pet_begin_transient()` 在选择缓存前统一设置瞬态状态和 `transient_started`，缓存命中和异步
  加载走同一套生命周期。
- 当前已经处于相同瞬态动作时，再次点击会把帧索引重置为 0、刷新首帧并重建下一帧 deadline；
  因此重复点击表示“从头再播放一轮”，不会把动画卡在中间。
- 异步加载完成回调不再单独负责启动瞬态，避免生命周期依赖某一种缓存路径。

### 回归方法

`runtime_install_serial.py --codex-pet-click-test` 通过 `pet.preview tap` 调用与物理点击相同的
`vb_pet_begin_transient()`。测试先建立非跳跃基线，再验证跳跃帧推进，在动画尚未结束时追加一次
点击，最后要求状态自动退出 `jumping`。这条测试必须使用 80 ms 的低延迟串口采样；普通
`codex_pet()` 状态读取为完整诊断预留 2 秒等待，会错过约 600 ms 的整轮跳跃动画并造成假失败。

真机修复前，回归稳定报：

```text
Codex Pet click test remained stuck in jumping after a repeated tap
```

修复固件刷入后，同一块板子实测为 `idle -> jumping(frame=2) -> idle`；随后九状态 sweep 全部
通过，帧数保持 `6/8/8/4/5/8/6/6/6`，`loaderPhase=0`、`droppedFlows=0`。

```bash
.venv/bin/python scripts/runtime_install_serial.py /dev/cu.usbserial-13220 \
  --codex-pet-click-test --ready-timeout 8 --no-echo
.venv/bin/python scripts/runtime_install_serial.py /dev/cu.usbserial-13220 \
  --codex-pet-sweep --ready-timeout 12 --no-echo
```

以后所有短暂动作都必须覆盖三条路径：首次异步加载、已加载缓存命中、动作尚未结束时重复触发。
只测试长期固定的 preview 状态无法发现这一类瞬态生命周期错误。

## 2026-07-25：三遍全量审查新增经验

本轮按“静态与自测、变更与高风险链路逐行审查、最终回归与运行态核对”执行三遍检查。重点不是
重复运行同一条命令，而是让三遍覆盖不同失效面：第一遍寻找语法、构建和协议错误；第二遍沿
`.hpet -> Companion -> BLE -> Runtime -> PSRAM/LVGL` 数据流检查边界；第三遍重新运行完整回归、
固件构建、历史包兼容和当前服务状态检查。

### 问题二十七：重复 loader 唤醒可能覆盖正在显示的缓存槽

#### 现象与根因

点击宠物或快速切换任务状态时，第一轮跳跃可以启动，但中途可能卡在一帧或出现撕裂。除了此前
已修复的“缓存命中没有启动瞬态生命周期”问题，异步加载路径还存在另一条独立竞态：状态变化
期间可以连续释放 loader semaphore。worker 完成一次加载后若直接领取一个陈旧 token，会按新
快照再次选择缓存槽；此时这个槽可能已经被 LVGL 激活，后台解压便会与前台读帧重叠。

#### 修复与回归

- loader 被唤醒后先用 `RT_WAITING_NO` 清空已排队 token，再读取一次最新目标状态，多个旧请求
  合并为一次实际加载。
- 合并后再次检查 stop 标志，避免退出阶段继续访问缓存。
- `runtime_architecture_audit.py --self-test` 固定检查非阻塞 drain，防止以后把 worker 改回逐 token
  执行。
- 瞬态动画仍必须同时覆盖首次异步加载、缓存命中和动画中重复点击三条路径；semaphore 合并不能
  替代瞬态生命周期测试。

### 问题二十八：主机曾允许签名结构损坏的 `VBPC v2`

#### 现象与根因

旧的 `.hpet` 校验只核对魔数、总长度和摘要。状态目录即使包含重叠块、尾随数据、损坏 zlib 或
与帧数不一致的解压长度，也可能先被主机签名并通过 BLE 传输，最后才在板端安装或加载阶段失败。
这会把本应毫秒级发现的转换错误伪装成耗时很长的部署失败。

#### 修复与回归

- v2 固定要求单宠物、160x173、9 状态，并要求状态块从目录末尾开始严格连续，禁止空洞、重叠
  和尾随数据。
- 使用有输出上限的 zlib 流式校验，要求 `eof`、无未消费/额外数据且解压长度精确匹配帧数。
- manifest 的逐状态帧数、总帧数和最大帧数必须与 `preload.bin` 目录逐项一致，不能分别合法却
  互相矛盾。
- 构建器在签名前校验，读取器在验签和 catalog 校验后再次校验；不能因为签名正确就信任内部
  二进制布局。
- 自测先用重叠、尾随和损坏 zlib 样本证明旧实现确实接受，再固定为拒绝回归。现有缓存中的
  6 个 `.hpet`（含 1 个九状态 v2）全部通过新校验和 Runtime 合成。

### 问题二十九：素材主机白名单可被 HTTP 重定向绕过

#### 现象与根因

JS 转换器和 Python Companion 只检查最初的 `assets.petdex.dev` URL，底层 HTTP 客户端会自动
跟随重定向。合法首跳因此可以把下载带到任意主机；带用户名密码或非标准端口的 URL 也没有被
明确拒绝。这既扩大了网络访问边界，也让不同实现的安全行为不一致。

#### 修复与回归

- URL 必须是 HTTPS、精确主机、无凭据、无 fragment 且使用默认 443 端口。
- JS 最多手动跟随 4 次重定向，每一跳先解析相对地址再重新检查；Python 使用自定义 redirect
  handler，在 `urllib` 发出下一跳请求前执行同一策略。
- 两侧自测都覆盖凭据、端口和跨主机跳转。网络失败仍允许使用已经校验过的 v2 摘要缓存，但
  安全策略拒绝不能降级为缓存命中。

### 问题三十：旧 `mainmenu.py` 已不能可靠生成菜单数据

脚本仍使用 Python 2 的 `print` 语法，在当前 Python 3 工具链中不能执行；同时 `r == 0` 分支只
计算 `2**12` 却没有 append，生成列表永远少一个元素。修复后所有分支统一追加数据，脚本改为
Python 3，并增加 `--self-test` 检查长度、零值路径和发射输出。以后即使脚本不是默认构建入口，
仓库级 Python 编译也必须覆盖它，不能把“目前没调用”当成语法和数据错误的豁免。

### 问题三十一：兜底 UI 和专用页面仍有物理屏安全区偏差

真机是 390x450 圆角 AMOLED，可用安全区固定为 `x=30..360`、`y=36..414`。本轮审计发现：
Codex Pet 连接文字右缘越过 x=360；Thunder Wing 暂停键不足 44x44；IMU 校准键低于 y=414；
Pager 多处文字从 x=20 开始且错误提示位于 y=410；通用 manifest action 仅高 34 px。

修复后专用页面全部收回安全区，交互目标至少 44x44。通用 manifest 组件改放在
`330x278` 的纵向滚动容器中，最多 8 项仍可访问，action 为 300x44；有 manifest 组件时不再让
底部诊断文字与组件重叠。静态审计仍会报告少量小型装饰对象、恰好落在安全边界的文字，以及
`#if 0` 中已停用的旧 App Manager；这些对象不承担触控，不是当前可达页面的裁切问题。新增或
重新启用停用页面时，必须重新按 390x450 实机规范审查，不能沿用这次豁免。

### 本轮验证结果与边界

- `runtime_deep_check.py` 全部通过，包括 22 个 Runtime 包、63 个 package case、完整可靠性组和
  Swift 47 项测试。
- `.hpet`、Petdex 转换、Companion、Web、Bridge、transport、架构审计和全部 tracked Python
  编译分别通过；固件从清洁依赖状态完成全量链接。
- 当前 `127.0.0.1:8790` 服务仍显示 Companion/Codex/板子全部连接，只有一个 Monitor/BLE owner。
- 2026-07-25 经用户授权，提交 `68bb8a5` 已刷入 `/dev/cu.usbserial-13220`；首次下载成功且 boot
  捕获确认 SD、CO5300、FT6146、Runtime、BLE 和 `preload v2 states=9` 全部正常。
- 点击回归实测 `idle(6帧) -> jumping(5帧) -> idle`，重复点击没有卡住；九状态 sweep 实测帧数
  为 `6/8/8/4/5/8/6/6/6`，全部 120 ms，`loaderPhase=0`、`droppedFlows=0`，UI tick 从 19
  连续增长到 272。
- 修正问题三十二后，180.001 秒 exercise soak 完成 28 次采样和 7 轮任务循环，最终
  `passed=true`，连接、投递、exercise、状态、动画、UI tick 和 flow 错误均为 0，Bridge RSS
  增长 112 KiB。该轮没有同时占用串口保存全程 fault 关键字日志，也没有新增正面真机照片，因此
  不能替代 24 小时发布 soak 或圆角玻璃的最终视觉验收。

### 问题三十二：exercise soak 与自动审批状态契约漂移

#### 现象与证据

首次 3 分钟实机 soak 在第一轮 `PermissionRequest` 后等待 30 秒并报
`approval-needed task aggregate did not converge`。但同一时间 `connected=1`、`state=running`、
`approval=0`、`activeTasks=3`、`loaderPhase=0`，UI tick 从 1597 增长到 1904；Hook 的
`deliveryFailures=0`。因此这不是 BLE、板端动画或 Hook 丢失。

#### 根因

`DesktopTaskRegistry.snapshot()` 已按既定安全契约处理自动审批：没有真实 approval ID 的通用
`PermissionRequest` 只是信息事件，必须归一为 `running / Approval handled / approval=0`；只有
真实待用户处理的请求才是 `needs_input / approval=1`。旧 soak 却强制等待
`needs_input / approval=0`，这个组合既与当前协议矛盾，也会把正确行为判为失败。

#### 修复与回归

- 新增 `informational_permission_handled()`，要求活动任务数达到目标且状态精确为
  `running / approval=0`。
- self-test 使用本次真机失败样本固定正确条件，并明确拒绝 `needs_input` 和 `approval=1`，防止
  以后再次混淆信息型 PermissionRequest 与真实实体审批。
- 修复后的完整 180 秒实机重跑完成 7 轮 exercise，35 个 Hook 全部送达，临时任务每轮从基线
  1 项增加到 3 项再清理回 1 项；`exerciseFailures=0`、`animationErrors=0`、
  `uiTickStalls=0`、`openOutage=false`。

```bash
PYTHONPATH=scripts .venv/bin/python scripts/codex_pet_soak.py \
  --duration-seconds 180 --sample-seconds 5 --exercise --exercise-seconds 20 \
  --minimum-exercises 5 --output .local/codex_pet_soak_3min.jsonl
```

## 2026-07-25：BLE 广播事件覆盖安装回包，部署在中途被误判失败

### 问题三十三：异步诊断和命令应答共用 GATT status 缓冲区

#### 现象与证据

Companion 部署 `2-mitsuha` 时在约 36% 失败，网页最后显示：

```text
Did not receive expected BLE status response. Last output:
ok status api=vibeboard-huangshan-ble-install/v1 active=welcome flow=272 secure=1 bulk=2
```

失败任务和 Monitor 日志还保留了三类关键证据：控制 ACK 偶尔读到旧的 `ok install_begin`，
`install_bulk` 曾返回 `rc=-1` / `rc=-7`，重连后又连续读到 `adv stopped` 或普通 `status`，导致目标
文件传输和旧宠物回滚都提前终止。板端 `df` 仍有约 3.6 MiB 空闲，主堆也有约 84 KiB 余量，
因此 SD 容量和内存耗尽不是根因。

#### 根因

Runtime 的 `vb_ble_advertising_event()` 在广播开始和停止回调中调用 `vb_ble_set_status()` 输出
诊断文本。这个函数写入的正是安装命令共用的 GATT status/notify 缓冲区。广播事件与连接、重连
和安装 ACK 异步发生，因此合法的 `install_begin`、`install_bulk` 或 `status` 回包可能在主机读取前
被 `adv started/stopped` 覆盖；主机看到的是语法正确但属于另一个时刻的旧消息。

主机 transport 还有第二层问题：控制面 `rc=-1` / `rc=-7`、bulk ACK 停滞或重连次数耗尽后，
旧逻辑直接终止整个部署。Runtime 安装是 staging 事务，这些错误应重新建立连接并从
`install_begin` 重启整轮事务，而不是只重发当前帧；但 `install_end` 已提交而 ACK 丢失仍属于
commit-uncertain，不能用同一策略盲目重装。

#### 修复与回归

- 广播开始/停止改为 `rt_kprintf()` 串口诊断，不再写共享 GATT status。异步事件今后只能写日志，
  只有命令 worker 可以发布命令应答。
- BLE v2 安装增加有上限的整轮事务恢复：`rc=-1`、`rc=-7`、bulk ACK 停滞和重连耗尽会先尽力
  abort staging，再重新连接并从 `install_begin` 开始；最多重启两次，避免无限循环。
- `install_end` 的 `InstallCommitUncertain` 保持独立裁决路径，防止板端已提交时再次安装或错误回滚。
- transport 自测固定覆盖 `rc=-1`、`rc=-7`、数据 ACK 永久停滞、恢复成功和重试耗尽；架构审计
  直接禁止 advertising callback 调用 `vb_ble_set_status()`，并要求保留串口诊断。
- 排障时先按时间线比对 Companion 期望 ACK、最后 GATT status 和广告事件；不能把“最后一条消息
  合法”误当成“它属于当前命令”。串口、广播和命令回包必须各自拥有明确的数据通道。

修复固件构建和启动确认通过。真实板子随后使用同一个摘要
`e3f8aea83249e09364160fcd89d9d1650aa6bd17fb3896348a7563f6a6c1b3cc` 重新部署
`2-mitsuha`，任务 `pet-827ca46b4e594718` 完成并报告 `assetStates=9`、`preloadVersion=2`、
`frameMs=120`、`preloadedBytes=1328640`。九状态 sweep 的帧数依次为
`6/8/8/4/5/8/6/6/6`，UI tick 从 19 增长到 273，`loaderPhase=0`、`droppedFlows=0`；电源接口
实测 VBAT 4355 mV、charger ready、当前 `no_charging`。现有 power API 没有经过标定的百分比
SOC 字段，不能从单次端电压臆算百分比。完整 Runtime deep check、Web、Bridge、transport 和架构
审计均通过。

### 问题三十四：BLE 广播停止后没有板端自愈

#### 现象与证据

2026-07-26/27，Companion 反复报告找不到固定的 `VibeBoard` 外设；按设备名和 Runtime
专用 Service UUID 做独立扫描均为 0 个匹配，而电脑蓝牙可以发现其他设备。打开串口会复位板子，
复位后的 `vb_runtime_ble_status` 立即显示 `init=1 power=1 service=1 adv=1`，并重新被扫描到，
说明射频、服务注册和广告数据本身正常，故障发生在运行后广告状态丢失。

#### 根因

板端只在 `BLE_GAP_DISCONNECTED_IND` 到达时投递广播重启；广告开始失败或广告被底层停止时，
`vb_ble_advertising_event()` 只更新诊断字段，没有安排恢复。若控制器漏发断连事件，
`link_active` 还会一直为 1，既没有广告也不会重试。此前断连处理还会无条件清掉
`link_active`，使非主连接的断开事件也能干扰当前主机链路。已有的 `is_auto_restart=1` 只能覆盖
SDK 能观察到的正常断连，不能覆盖启动失败、控制器复位或事件丢失。

#### 修复与回归

- 广告 started/stopped 回调只写串口诊断；失败或停止会向 BLE worker 投递恢复意图，绝不在回调
  中直接重入广告 API。
- BLE worker 每 1 秒做健康检查，通过 `connection_manager_get_connection_state()` 校验活动连接
  索引；超过 5 秒仍无效的连接会清理 host/Notify 状态并恢复广播。
- 广播恢复受 `power_on/service_ready/link_active/connected/advertising` 五项门闩保护，
  有效连接期间不重启、不抢占 GATT；重复恢复请求会被限速重试。
- 断连按 `conn_idx` 隔离；只有当前 host/raw 链路的断连才清理 host 状态并安排广播恢复，
  非主连接只清理自己的 MTU/安全请求记录。
- 架构审计固定检查健康检查、连接索引和“不在有效连接期间重启广告”的门闩。
- 重启后的串口状态必须看到 `adv=1 starts>=1`；主机侧 fresh scan 必须重新找到 `VibeBoard`，
  建连后保持 `secure=1`、Notify 正常，断开后在有界时间内再次发现广告。

2026-07-27 实机回归：最终固件编译、烧录均成功；复位后串口显示
`service=1 adv=1 starts=1 stops=0`。macOS fresh scan 找到固定身份
`22614720-A687-453C-8D11-DD5297EA6FB6`；
30 秒连接保持期间每 3 秒一次的 9 次 `status` 均返回 `secure=1`，客户端主动断开后再次扫描仍能
找到 `VibeBoard`。Companion 随后在 6.6 秒内连上，`/v1/status` 返回 `board.connected=true`、
`runtimeApi=vibeboard-huangshan-runtime/v1`、`bleInstall=true`。架构审计、Runtime transport、
BLE CLI、Bridge 和 Companion 自测通过。

### 问题三十五：macOS Companion 仍依赖源码环境，且首次蓝牙扫描阻塞网页

#### 现象与证据

旧 `.app` 只是一层 Swift 启动器：它从 App 所在目录反推仓库，再运行 `.venv/bin/python` 和
`scripts/codex_pet_bridge.py`。离开开发机或删除源码后必然启动失败，宠物转换还要求用户预装
Node/Sharp。把 Python 初步打包后，发布校验又发现 ad-hoc Hardened Runtime 会因 Agent 与内置
Python framework 的 Team ID 不同而拒绝 `dlopen`。

真实启动测试还发现 Agent 进程存在但 `127.0.0.1:8790` 长时间拒绝连接。日志停在
`[codex_pet][ble] connect attempt`：monitor 先等待最长约 170 秒的 BLE 连接，完成后才创建 HTTP
服务。板子关机或首次配对时，新用户会把正常扫描误判为 Companion 已崩溃。

#### 根因

- 消费级 App 与开发仓库没有形成资源边界，Hooks 命令也固定引用 Python 和源码脚本。
- PyInstaller 的 Python framework、PyObjC/CoreBluetooth 与 Node/Sharp 都属于嵌套可执行依赖，
  必须按签名类型采用一致的签名策略。
- `run_service()` 把 UI 服务错误地放在设备 ready gate 之后；BLE ready 是硬件状态，不应成为本地
  设置页的可用性前置条件。
- 公网域名到 loopback API 还涉及精确 CORS、Private Network Access 预检和短期 capability，
  只注册一个深链不足以形成完整网页交接。

#### 修复与回归

- 新增统一 `codex_pet_agent.py` 和 `companion_paths.py`。PyInstaller App 内置 Python、Bleak、
  PyObjC/CoreBluetooth、Hooks 与静态资源；Node、Sharp 和九状态转换器也放入 App Resources。
  Hooks 命令改为应用内 Agent，并保留可审计的 `codex_pet_hook.py` 资源路径。
- Swift 外壳改为菜单栏 App，支持首次设置、登录启动、状态轮询、诊断日志、更新检查、Agent
  异常重启、`vibeboard://companion/open` 和经过 slug/digest 校验的安装深链。启动前先探测
  `/v1/status`：菜单栏进程被强杀但 Agent 仍存活时接管现有服务，不重复启动第二个 BLE owner，
  避免 8790 端口冲突和两秒一次的重启循环。新 Agent 同时接收 `--parent-pid`，每 2 秒检查父进程；
  外壳被强杀后会自行关闭 HTTP、IPC 和 BLE，不长期遗留孤儿进程。
- ad-hoc 开发包不启用 Hardened Runtime；正式 Developer ID 包继续使用 runtime、timestamp 和
  inside-out 签名，再执行 notarytool、staple 和 SHA-256。正式签名构建禁止借用其他 App 的
  Sharp，必须使用项目依赖。
- Companion HTTP 在 `service.start()` 等待 BLE 之前启动。修复后用真实板子验证：网页状态接口
  立即可访问，BLE 随后约 7 秒连接，`board.connected=true`。
- 公网 Origin 只能在构建时精确配置；loopback API 响应 PNA 预检，写请求仍要求 15 分钟 token。
  未启动时网页显示启动/下载 Companion，不向公网暴露 BLE 或 Codex 凭据。
- `verify_codex_pet_companion_app.sh` 对成品执行 codesign、CoreBluetooth/PyObjC 导入、Hook、
  Companion、Bridge、`.hpet`、Ed25519 和 Sharp 转换自测。最终 arm64 开发 App 与 62 MB DMG
  构建成功，整套成品校验通过。

#### 后续规则

1. 任何消费级 Companion 发布都必须在没有仓库、Python 和 Node 的干净 Mac 上验证。
2. 本地设置/API 必须先于慢速硬件发现可用；连接状态只能影响按钮，不能阻塞页面监听。
3. ad-hoc 与 Developer ID 签名不可共用同一组 Hardened Runtime 假设；成品必须实际执行内置
   Agent，不能只检查 `codesign --verify`。
4. 公网站点只允许精确 HTTPS Origin，不能用 `*` 绕过 CORS/PNA；深链和 API 参数都必须继续
   校验 slug 与 digest。
5. 菜单栏外壳与 Agent 是两个进程；启动、崩溃恢复和登录启动都必须先接管健康实例，再决定是否
   创建新实例，不能把父进程 PID 当作唯一生命周期真相。

### 问题三十六：Runtime 重载卡在 Lua 关闭阶段，桌面永久停帧

#### 现象与真机证据

2026-07-27，板子桌面停止刷新，但显示屏和触控诊断仍分别报告 `ready=1`。串口连续两次读取
Runtime App Manager 得到完全相同的状态：

```text
active=codex_pet state=idle running=0 failed=0
pending_reload=0 reloading=1 pending_stop=0
```

同时 Codex Pet 状态仍为 `active=1`、`preloadVersion=2`、`assetStates=9`、
`preloadedBytes=1328640`，但 `uiTicks` 停在 `5` 不再增长。这说明屏幕、触控、宠物资源和状态
查询线程尚可工作，实际停止的是 GUI timer 所在的 Runtime 重载路径。现场 Companion 日志只有
`GET /v1/status` 轮询，没有新的部署、launch 或 reload 命令，因此不能把这次卡死归因为网页重复
部署或 Companion 重复下发。

#### 已确认根因

`vb_runtime_reload_current()` 先将 `reload_in_progress` 置为 1，再同步调用
`vibeboard_lua_stop_app()`；只有函数完整返回后才会清除该标志。现场状态中旧宠物仍
`active=1`，而真正的 `vb_codex_pet_stop()` 位于 Lua Host stop 之后。因此卡死点已收敛到
`vibeboard_lua_stop_app()` 内、Host stop 之前的 `lua_close(g_vb_lua.state)`：Lua state 关闭没有
返回，GUI timer 无法继续执行，`reloading=1` 也就永久保留。

当前诊断没有捕获 Lua 内部堆/GC 的具体栈帧，不能把根因进一步武断写成某个 allocator 或 LVGL
缺陷；但“重载流程同步阻塞在 Lua close，且没有阶段诊断、超时恢复或失败出口”已经由状态顺序
和代码控制流确认。宠物名称标签的删除只改变渲染对象，不经过 Runtime reload 或 Lua close，不是
本次故障原因。

另发现一个必须同时治理的同类风险：`vb_pet_stop_preload_worker()` 在 GUI 路径中无限等待
`loader_thread` 变为 `NULL`。本次状态仍为 `active=1`，所以它不是本次已证实的直接卡点；但未来
一旦卡在宠物清理阶段，该无界等待会造成相同的“桌面永久停帧”。

#### 恢复与永久修复方案

- 现场恢复：硬件重启后板子已恢复正常。这只会重新初始化 Runtime，不能视为永久修复，也不能
  代替后续回归。
- 为 Runtime reload 引入可读的阶段状态和开始时间，至少区分 `lua_close`、host stop、root 删除、
  包加载和 Lua start；`vb_runtime_app_status` 必须暴露阶段和持续时间。以后不能只看到一个
  `reloading=1` 就猜测卡点。
- 对正常重载路径建立失败收敛机制：重载进入后必须保证能回到“已运行”或“已报告失败并可重新启动”
  两个终态之一。恢复动作只能由 Runtime/GUI 所有者安排，BLE、串口或 watchdog worker 不得直接
  删除或创建 LVGL 对象。
- 针对 `lua_close` 阶段，先用新增阶段日志和真机重放取得 Lua 堆/GC 证据，再修复触发关闭阻塞的
  底层资源生命周期；不要在 GUI 线程里用忙等或强行跨线程销毁 Lua/LVGL 来掩盖问题。
- `vb_pet_stop_preload_worker()` 改为有界停止协议：发出 stop、唤醒 worker、在有限时间内等待其
  确认；超时必须记录错误并走受控恢复，不能无限 `rt_thread_mdelay(10)`。释放 semaphore 和 PSRAM
  资源只能发生在 worker 已确认退出之后。

#### 回归要求

1. 冷启动、BLE 连接、状态轮询并存时，连续执行至少 20 次 `codex_pet` reload；每轮都要求
   `running=1`、`reloading=0`、`uiTicks` 持续增长，并且 `active=1` 的九状态资源可用。
2. 在 jumping/running/review 等会唤醒预加载 worker 的状态切换期间触发 reload，验证 worker 停止、
   Lua close、root 重建和预载恢复均在有界时间内完成。
3. 故障桩分别阻塞 Lua close 和预加载 worker；状态接口必须给出具体阶段和失败原因，板子不得只
   留下无期限的 `reloading=1`。恢复后必须重新通过显示、触控、BLE、九状态 sweep 和 UI tick 检查。
4. Companion 的常规 `/v1/status` 轮询不得触发 reload；日志和自动化测试要固定这一负向契约，避免
   后续排障再次把只读轮询误判为部署命令。

### 问题三十七：恢复路径不能以同步清理或未校验刷写为代价

#### 问题根源

问题三十六中的 `lua_close()` 在 GUI timer 内同步执行，导致一个 VM 关闭异常就能永久阻塞所有
LVGL 刷新。宠物的预加载 worker 也存在同一类风险：旧实现发出停止信号后，在 GUI 路径无限等待
`loader_thread` 变为 `NULL`。即使 Lua close 已经修复，SD 卡读写或 zlib 解压异常也仍可通过这个
无界等待把桌面卡住。

固件恢复路径的风险不同但同样严重：原有 `flash.py` 能可靠刷写当前 build，却没有“这一组
bootloader、flash table 和 Runtime 是否来自同一已验证版本”的发布身份，也没有签名校验。把
BLE 宠物包安装和全量固件刷写混为一谈，会让用户在普通部署失败时走向破坏性恢复操作。

#### 已落地的解决方案

- Runtime reload 改为 GUI 所有者驱动的阶段状态机。`lua_close` 在独立 RT-Thread worker 执行，
  GUI timer 只轮询 `lua_close -> host_stop -> teardown -> load`；`app_status` 新增
  `reload_phase`、累计耗时、重载次数、失败次数和超时次数。BLE/串口 worker 仍然只能发布 reload
  请求，不能跨线程触碰 LVGL。
- `lua_close` 和 `host_stop` 均有五秒超时。超时后旧 UI、宠物和资源仍被保留，状态收敛为
  `recovery_required` 并带明确错误；绝不在未知 worker 尚在访问 Lua、LVGL 或 PSRAM 时删除 root
  或释放缓存。受控重启可以恢复，后续重试则会等待旧 worker 的确认。
- 宠物预加载 worker 的停止协议改为非阻塞 acknowledge：先设置 stop 并唤醒 semaphore，worker
  完成 SD/zlib 工作后自行清空 thread 句柄。`host_stop` 返回 `-RT_EBUSY`，由 Runtime timer 轮询，
  而不是 `rt_thread_mdelay()` 忙等。只有 worker 已退出时才删除 semaphore 和释放预加载 PSRAM。
- 新增 `scripts/firmware_release.py` 和 `docs/firmware-release-and-recovery.md`。每个 USB 固件
  发布包都必须包含并签名 `firmware-release.json`，其中锁定 board、版本、sftool flash 地址、文件
  长度和 SHA-256。升级和回退都先用外部钉住的 Ed25519 公钥验证，再调用 `flash.py`，并要求 boot
  日志确认。回退是刷入上一个已验证的完整 release，不能从当前板子猜测或读取未知镜像。
- Companion 状态明确公开 `verified_usb_recovery` 与 `wirelessDfu=false`。当前 flash table 构建时
  会报告 `img "dfu" not found`，所以双镜像无线 DFU 在完成分区迁移、swap 断电测试和真机回退前
  不得向用户开放，更不能把 Runtime App 的 BLE 安装描述为固件升级。

#### 回归要求

1. 连续执行至少 20 次宠物 reload；每轮检查 `reload_phase` 最终回到 `idle`、`reloading=0`，
   `uiTicks` 持续增长，且九个资产状态仍可切换。
2. 人为让 Lua close 或预加载 worker 不返回时，五秒内状态必须变为 `recovery_required`；屏幕仍应
   保持可见，不能黑屏、死帧或释放正在使用的缓存。
3. 每个发布候选必须执行 `firmware_release.py create`、`verify` 和 `apply --dry-run`；仅在 USB
   真机确认启动后记录为 last-good。回退候选必须使用同一流程刷入上一个签名 release。

### 问题三十八：固件恢复和诊断能力必须与普通宠物部署隔离

#### 问题根源

Runtime App 的 BLE 安装已经可以稳定部署九状态宠物，但这条链路不具备
bootloader、flash table 或 Runtime 固件的完整性保证。把它直接扩展成固件
升级会在单分区设备上留下不可启动的半成品。与此同时，Companion 的诊断
包如果按目录中的“最新 ZIP”返回，在两个任务并发完成时可能下载错任务，
并且固件下载 ZIP 如果未先检查成员路径，会带来路径穿越风险。

#### 已落地的解决方案

- 固件发布包由 `firmware_release.py` 生成并用钉住的 Ed25519 公钥验签，锁定
  board、地址、长度和 SHA-256；更新、健康检查、last-good 记录和自动回滚
  由 `companion_firmware.py` 统一编排，和宠物 BLE 安装保持不同 API/状态。
- 下载的固件 ZIP 在解压前逐项拒绝绝对路径、`..` 路径和符号链接；验证通过后
  才能进入临时目录，避免归档文件写出 staging 根目录。
- 双分区方案由 `dual_bank_dfu.py` 作为迁移门禁和离线故障模拟工具维护。当前
  `wirelessDfu=false`，因为生产 flash table 仍是单分区；不能通过 Companion
  或 Runtime 隐式开启。
- 一键诊断通过 `companion_diagnostics.py` 生成每任务唯一文件名，job 结果保存
  `bundlePath`，下载时校验固定文件名和 support 目录边界。内容只保留有限日志尾部，
  并递归清理 token、密码、私钥、凭据和 Hook command。

#### 回归要求

1. `python3 scripts/firmware_release.py --self-test`、`python3 scripts/dual_bank_dfu.py self-test`
   和 Companion 自测必须通过。
2. 构造包含 `../escape`、绝对路径和符号链接的固件 ZIP，必须在验签前拒绝并且 staging
   目录外没有新文件。
3. 并发创建两个诊断任务，分别只能下载自己的 `bundlePath`；ZIP 内不得出现 token、password、
   private key 或 Hook command。
4. 真实板子更新仍须 USB boot-log、Runtime health 和九状态 sweep；无线 DFU 在 ptab/bootloader
   迁移完成前必须保持关闭。

### 问题三十九：动态诊断 JSON 不是一次性快照

#### 问题根源

BLE/串口 Runtime 状态包含心跳、耗时和恢复计数。读取 `json_read` 的过程中字段可能从 576
字节增长到 577 字节，旧客户端按首次返回的总长度读取会误报 JSON 长度不一致，导致诊断或部署
流程失败。另一个现场陷阱是重新打开串口会触发 USB bridge 冷复位，跨进程保存的故障注入计数不能
用来证明同一次测试仍在运行。

#### 已落地的解决方案

- `runtime_transport.py` 在 `json_read` 总长度变化时从 offset 0 重新开始读取，并限制重试次数；
  不再把动态字段变化误判为设备损坏。
- 重试故障和 watchdog 故障测试使用同一个 SerialTransport 会话，测试脚本明确记录冷复位边界，
  避免把新启动的板子状态当成注入后的状态。
- `runtime_recovery_soak.py` 将 20 次 reload、九状态 sweep 和点击跳动检查固化为可重复的真机门禁。

#### 结果

最新固件已通过 20 次 Runtime reload（失败/超时均为 0）、九状态 sweep、点击跳动恢复、一次注入
重试故障和一次注入 GUI heartbeat watchdog 重启；watchdog reset reason 为 `0x56420001`，板子
重启后重新广播并恢复连接。

### 问题四十：本地 Companion 的恢复与诊断接口也必须按生产边界审计

#### 问题根源

这一轮完整审查发现，板端 Runtime 已具备恢复保护，但桌面侧仍有几处容易在真实用户并发操作或
异常网络条件下暴露的问题：

- `GET /v1/jobs/<id>` 能返回安装、固件和诊断任务的日志，却没有像下载诊断 ZIP 一样检查 session；
  loopback 服务面对公网网页时，这会扩大同机其他页面读取任务细节的机会。
- 诊断 ZIP 只按秒命名；两个任务在同一秒完成会覆盖同一文件。宠物 spritesheet 缓存也复用固定
  `.tmp` 文件，多个请求拉取同一资源时可能竞争写入。
- 诊断脱敏只覆盖简单 Bearer/token 字样，不能可靠覆盖 `Authorization: Basic ...`、`Cookie: ...`
  或 PEM 私钥；支持包恰恰是最不该泄漏这些值的文件。
- Petdex manifest、固件 feed 和 release 初始地址要求 HTTPS，但默认 HTTP 客户端会自动跟随未审计
  的重定向；固件包最终仍会验签，然而下载边界和错误提示不应依赖“最后一步会失败”。
- 固件 update 和 rollback 可以同时被 API 创建为任务。底层 USB flash 不是可并行资源，两个操作可能
  同时写状态文件、争用串口或让 UI 显示一个迟到的失败任务。

#### 已落地的解决方案

- 任务查询、诊断创建和诊断下载统一要求有效的 15 分钟 Companion session；仍保持服务只监听
  loopback 和精确 Origin/PNA 校验。
- 诊断文件名加入经校验的 job ID（无 job ID 时使用随机后缀），下载时继续同时检查文件名白名单和
  support 根目录。spritesheet 使用唯一临时文件、`fsync` 和原子 `replace`，不会把半写缓存暴露给
  并发读取者。
- 脱敏器递归处理数据结构，并覆盖 Bearer/Basic、Authorization、Cookie、API key、token、密码和
  PEM 私钥；字段名会保留而值替换为 `[redacted]`，使支持包既安全又可定位来源。
- Petdex manifest 的初始 URL 和每次重定向都只能留在 `https://petdex.dev`；固件 feed/release 及每一次跳转只能是
  无账号、无片段、标准端口的 HTTPS URL。固件 release 仍必须通过钉住 Ed25519 公钥、哈希和布局验证。
- `FirmwareManager` 为 update/rollback 共享非阻塞操作锁，第二个操作立即得到明确错误，不会碰触板子
  或覆盖恢复状态。

#### 回归要求

1. 无 token 查询 `/v1/jobs/<id>` 和下载 `/v1/support/bundles/<id>` 必须返回 401；带当前 session
   才能读取该本地 Companion 保存的任务结果。
2. 同时创建两个诊断任务，ZIP 名称不同、下载路径不能越过 support 目录，且压缩包和日志中不出现
   Bearer/Basic 凭据、Cookie、token、password、private key 或 Hook command 的原始值。
3. 伪造 HTTP、带账号、非标准端口或带 fragment 的 Petdex/firmware URL，以及重定向到这些地址的响应，
   必须在下载前拒绝；有效 release 仍须完整验签。
4. 并发请求 update 和 rollback 时，只有一个可以持有 USB 恢复操作；失败请求不写 `last-attempt.json`
   且不启动刷写子进程。
5. `companion_diagnostics.py --self-test`、`companion_firmware.py --self-test`、
   `codex_pet_companion.py --self-test`、`runtime_architecture_audit.py --self-test` 和真实 Companion
   API 回归必须全部通过后才可打包发布。

### 问题四十一：固件变更后 CoreBluetooth 缓存旧 GATT handle，并且异步日志污染状态通道

#### 问题根源

一次开发刷写后，板子仍稳定以 `VibeBoard` 广播，macOS 也能建立加密链路，但读取 Runtime `status`
特征返回单字节 `0x01`。该值是旧 GATT 表里的 CCCD 内容，不是 Runtime 状态；Companion 因此一直
等待 `status`/`capabilities` 的有效回包，网页最终显示连接或部署超时。

仅换一组 Runtime UUID 不能修复这个问题。板端已实际注册新 UUID，CoreBluetooth 却连服务枚举也按
同一 peripheral identity 复用了旧表。换 UUID 会同时破坏已发布 Mac/iOS 客户端兼容性，并没有让
当前设备自动恢复。

缓存失效后又发现第二个独立遗漏：`vb_ble_advertising_start()` 把 `adv start requested`、重启请求和
分配失败等异步诊断写进了命令应答共用的 `status` characteristic。即使 GATT handle 正确，首次读取
也可能不是命令回包。主机端还直接 await 了 Bleak 的 `__aenter__` 和 `__aexit__`；CoreBluetooth 在
半开连接或 Service Changed 的关闭阶段不返回时，单条诊断命令会永久占住板子的唯一中心连接。

#### 已落地的解决方案

- `project/proj.conf` 显式开启 SDK 的 `CONFIG_BLE_SVC_CHG_ENABLE=y`。Runtime 上电时设置一次标准
  `Service Changed` indication；已绑定中心在加密后会重新发现同一 v1 Runtime service 的正确
  attribute table。Python、iOS 和已发布协议 UUID 保持 v1，不以临时 UUID 轮换作为缓存恢复手段。
- 广告 started/stopped 之外，`vb_ble_advertising_start()` 的 already-started、restart-requested、
  allocation/init failure 也全部改为 `rt_kprintf()`。`status` 缓冲区只保留由命令 worker 生成的
  应答。架构审计同时覆盖广告 callback 和广告启动函数，防止将来把异步日志重新写回 GATT status。
- `BLETransport` 给 Bleak 建连、两条 notify 的取消和 `__aexit__` 统一加 GATT I/O deadline；即使
  CoreBluetooth 清理回调挂住，transport 也会清空本地引用、释放 Companion 命令队列，并让板端广告
  健康检查恢复可发现状态。自测包含永不返回的 `__aexit__` 桩。
- 真机恢复先释放失效客户端，再做 fresh scan；不要把“广告在有效连接时停止”误诊为广播故障。
  板子在中心连接存在时按 BLE 规范停止可连接广告，连接关闭后才应检查健康恢复。

#### 真机回归结果

- 在同一台曾缓存旧 handle 的 Mac 上，标准 Service Changed 后 `status` 命令返回
  `ok status api=vibeboard-huangshan-ble-install/v1 ... secure=1`；`capabilities` 经 BLE 读取完整
  713 字节 JSON 后进程退出，未留下半开连接。
- 最新固件实际跑完 1 次 Runtime reload（1852 ms，`reload_failures=0`、`reload_timeouts=0`）、九状态
  sweep（`idle/runRight/runLeft/waving/jumping/failed/waiting/running/review`，每项均有多帧且 UI tick
  前进），点击 jump 后回到 `idle`。
- 电源 API 返回 VBAT 4277 mV、charger `no_charging`。该硬件当前没有经校准的电量百分比 SOC，不能
  从单次电压伪造百分比。
- `runtime_transport.py`、`runtime_architecture_audit.py`、Bridge、Companion、Swift package、固件
  构建和打包后的 Companion 验证均通过。

#### 后续规则

1. 任何固件 GATT 表调整必须走标准 Service Changed，并在“旧 bond、新固件”的同一台 Mac 上做读、
   notify、完整 JSON 和断开后重扫回归；fresh scan 或改 UUID 不是 GATT cache 的替代方案。
2. 命令应答 characteristic 只能由命令路径写入。广告、连接、重启、健康检查和采样诊断必须走串口
   日志或独立 telemetry 通道。
3. 所有外部 BLE await，尤其是 client 建连和关闭，都必须有上界；超时后本地 transport 必须可丢弃，
   不得继续占用下一轮重连或部署任务。

## Companion 任务恢复、缓存上限与健康检查（2026-07-29）

#### 现象

一次 Petdex 宠物部署在下载阶段遇到临时网络错误。旧实现随后直接尝试本地九状态缓存；缓存没有
对应宠物时，最终只显示“没有已验证缓存”，掩盖了最初的网络根因。与此同时，Companion 的任务
只保存在进程内存，服务重启会丢失部署进度；长期构建多个宠物也会让 `.hpet` 缓存无限增长。

#### 根因

- 宠物源下载没有针对超时、连接失败、408/425/429/5xx 的有限重试，且缓存回退错误覆盖了源错误。
- `CompanionJob` 没有持久化边界，队列、传输和失败状态无法跨进程恢复或审计。
- 包文件按 digest 写入但没有数量/容量策略，当前包和回滚包的保护规则也没有集中管理。
- 网页只有 `/v1/status`，无法区分“服务进程正常但资源配置坏了”“服务正常但板子未连接”和“可以
  开始部署”这三种产品状态。

#### 已落地的解决方案

- `build_hpet_petdex.js` 对可重试的网络错误使用 400/800/1600 ms 退避；重定向、响应大小和内容
  校验仍是不可重试的安全错误。`CompanionState` 保留源错误，并在缓存回退失败时合并两条原因。
- 新的 `scripts/companion_state.py` 提供原子写入的 `JobJournal`。任务带有 `createdAt`/`updatedAt`，
  进度和日志按 250 ms 窗口合并写入 `jobs.json`，终态强制落盘，避免每个 BLE 分片都 `fsync`；
  服务重启时将非终态任务转换为 `failed + interrupted`，不再把旧任务伪装成运行中。
  `GET /v1/jobs` 只允许已授权会话读取完整列表。
- 同一模块提供 `PackageCache`，默认最多 24 个 `.hpet`、128 MiB，部署时保护当前 digest 和上一个
  digest，超限按最旧文件淘汰。`/v1/status` 返回缓存占用和任务计数，便于支持诊断。
- 新增只读 `GET /v1/health`，返回 `serviceReady`、`boardReady` 和逐项检查结果。网页部署按钮同时
  检查健康状态、Codex Hooks 信任和板子连接；重启中断任务会显示明确的中断原因。

#### 后续规则

1. 任何异步用户任务必须有可恢复的持久状态；状态更新和日志写入必须使用原子替换，启动时必须处理
   非终态记录。
2. 所有本地资源缓存都要定义最大条目数、最大字节数和不可淘汰的活动/回滚对象，并在状态接口公开
   当前占用，不能依赖用户手动清理磁盘。
3. C 端入口先调用 `/v1/health`，再允许部署；`serviceReady` 和 `boardReady` 不得用一个布尔值
   混淆服务故障、未连接和可部署状态。

### 问题四十二：BLE 部署失败由旧 Companion 进程和延迟 ACK 共同触发

#### 现象

网页在宠物包已下载、组合完成后报错：

```text
BLE install begin interrupted: Did not receive expected BLE status response
```

一次失败停留在 `Starting transactional install`；另一次在分包传输中收到
`ok install_bulk_data` 后重连并最终失败。板子仍可被扫描为 `VibeBoard`，因此不能直接把
该现象归因为板端停止广播或宠物包损坏。

#### 已确认根因

这是两个会叠加的主机端问题：

1. 已重建 `.local/VibeBoard Companion.app`，但实际持有 CoreBluetooth 连接的旧 App/Agent
   进程仍在运行。磁盘上的新包不会替换已加载到内存的 Python transport；旧进程继续使用已超时的
   CoreBluetooth 会话，`connect` 最终达到 deadline。只看网页或构建成功不能证明修复已生效。
2. BLE status notification 是异步队列。上一帧的成功 ACK 可以在主机开始等待下一帧时才到达。
   旧 transport 将该 ACK 当作当前帧不匹配，进入重试并清空队列，连同随后到达的正确 ACK 一起丢弃。
   主机为重连主动关闭 GATT 时，板端出现的 `host disconnected reason=19` 是这个恢复动作的结果，
   不能误判为板子先断开。

另一个放大因素是 `install_begin` 可能清理 SD 卡上的 staging 目录，重连后加密和 CCCD 恢复也需要
时间；普通 Runtime 命令使用的 4 秒窗口不足以判断安装控制命令失败。

#### 已落地的解决方案

- `runtime_transport.py` 新增 `ble_bulk_ack_is_stale()`：只要 ACK 属于同一 transfer id 且 offset 或
  next sequence 落后于当前等待帧，即保留通知队列并继续等当前 ACK。未知 transfer id、错误 ACK 和
  不符合当前事务的确认仍然按异常处理，不能无限忽略。
- 所有 `install_begin`、`install_end`、事务状态确认、abort 和重连验证统一至少使用
  `BLE_INSTALL_CONTROL_ACK_TIMEOUT_SECONDS = 20`。这只放宽安装控制路径，普通命令仍使用原有响应
  时间，避免全局超时掩盖故障。
- Companion 在安装失败后进入明确的 `rollback` 阶段，持续报告恢复旧宠物的进度，成功恢复后再返回
  原部署失败。页面不再长时间停留在上一笔传输进度而看不出正在回滚。
- 部署验证前必须停止并确认退出实际运行的 App/Agent PID，再启动刚构建的 bundle；通过
  `/v1/status` 确认 `board.connected=true`、`bleInstall=true`，不能只根据进程存在或网页可打开判断。

#### 回归与真机证据

- `runtime_transport.py --self-test` 增加“先到上一帧 ACK、后到当前 ACK”的桩，断言单次写入即可
  成功，防止后续修改重新清空正确的 ACK。
- `codex_pet_companion.py --self-test`、`verify_codex_pet_companion_app.sh` 和补丁格式检查均通过。
- 重启新版 Companion 后，板子自动恢复连接。真实部署宠物 `002` 完成 `install_begin`、`5038` 个
  数据块传输、提交和激活确认，作业到达 100%；板端返回 `pet=002`、`assetStates=9`、`frames=6`。

#### 后续规则

1. 每次改动 Companion 运行时代码后，发布或真机验证前都要同时记录新 PID/启动时间、
   `/v1/status` 的 BLE 能力和实际安装作业终态；重建产物本身不是验证。
2. BLE 事务 ACK 必须按 transaction id、offset 和 sequence 关联。对于可证明属于较早帧的 ACK，继续
   等待；不要立即断连、清队列或重发当前帧。对于身份不符、错误码或超出事务边界的消息，仍须显式
   失败并走有限恢复。
3. 观察到 `reason=19` 时，先核对 Companion 是否因重试主动关闭客户端，再判断板端广告/连接自愈；
   排障必须按主机写入、通知到达、连接关闭的时间线归因。
4. 新的 BLE 安装修复必须至少跑一次真实大于 1 MiB 的九状态宠物，覆盖 staging 清理、长分包传输、
   提交确认和回滚 UI，不得只用小型模拟包判断成功。

## 待继续沉淀的问题

后续遇到下面类型的问题，也应补充到本文档：

- Runtime App 安装成功但启动失败。
- Web App Store 显示超时或缓存状态误导。
- App 在圆角屏安全区被裁切。
- BLE 分包安装失败、断连或重试异常。
- 新增 helper 后 iOS / Web / Python 校验器不一致。
- Lua 脚本对象数量、脚本大小、资源大小触顶。
- 图片、字体、LVGL 样式在真机上和预览不一致。
