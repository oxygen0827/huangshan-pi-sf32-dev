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

## 待继续沉淀的问题

后续遇到下面类型的问题，也应补充到本文档：

- Runtime App 安装成功但启动失败。
- Web App Store 显示超时或缓存状态误导。
- App 在圆角屏安全区被裁切。
- BLE 分包安装失败、断连或重试异常。
- 新增 helper 后 iOS / Web / Python 校验器不一致。
- Lua 脚本对象数量、脚本大小、资源大小触顶。
- 图片、字体、LVGL 样式在真机上和预览不一致。
