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
- 当前产品策略明确为单一 active 宠物；导入新宠物时替换 active 资源，而不是让板端同时
  预存无限宠物。
- Runtime 包校验器检查 manifest、路径、状态帧、包大小和目录安全性；安装后由串口/BLE
  传输层逐文件发送并在 staging 提交。
- 板端启动日志报告实际预载的宠物数、状态数、帧数、原始字节和压缩字节，不能只看安装
  命令返回成功。

#### 后续规则

1. 新宠物验收顺序固定为：源站帧校验 -> Runtime 包校验 -> 安装 -> `.active` -> 冷启动日志
   -> 五状态真机观察。
2. 任何状态帧缺失都应在导入或包校验阶段失败，不要等到真机运行时才回退。
3. 记录当前 active 宠物名称和包校验结果，避免“电脑端喜欢的宠物”和“板端实际运行的宠物”
   混淆。

#### 验证方式

```bash
node scripts/import_petdex_pets.js --check-config
node scripts/extract_codex_rocky.js --check
python3 scripts/runtime_package.py --all
```

板端确认 `active=codex_pet`、`preloaded pets=1`，并逐个触发 idle/running/done/error。

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
- 回归必须记录：slug、运行时文件总字节、命令数、最大命令长度、动态 deadline、`frames=2`、
  `frameMs=180`、`preloadedBytes=830400`，并在真实板子上连续部署至少 10 次后再发布。

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
- BLE 安装命令使用 ATT Write Command（无 ATT write response），随后严格等待 Runtime status
  characteristic 的 `install_begin/file/end` ACK。这样不会让 ATT 写响应和 worker 的 SD/SPI
  工作互相竞态；普通状态和控制命令继续使用原来的 Write Request。
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
| BLE 安装写入 | Write Command | 可靠性由 Runtime status ACK 提供 |

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
`frames=2`、`preloadedBytes>0`、`queuedFlows=0`、`droppedFlows=0`，并且串口无 SPI/BLE
故障关键字。

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

## 待继续沉淀的问题

后续遇到下面类型的问题，也应补充到本文档：

- Runtime App 安装成功但启动失败。
- Web App Store 显示超时或缓存状态误导。
- App 在圆角屏安全区被裁切。
- BLE 分包安装失败、断连或重试异常。
- 新增 helper 后 iOS / Web / Python 校验器不一致。
- Lua 脚本对象数量、脚本大小、资源大小触顶。
- 图片、字体、LVGL 样式在真机上和预览不一致。
