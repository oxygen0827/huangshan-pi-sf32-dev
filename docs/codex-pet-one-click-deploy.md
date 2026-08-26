# Codex Pet 一键部署

本实现采用“Codex Pet 网页 + macOS VibeBoard Companion + 单一 BLE Runtime”的结构。
网页不直接持有 Bluetooth，也不读取 Codex 凭证。`codex_pet_bridge.py --mode monitor`
是唯一 BLE owner，同时启动只监听 loopback 的 Companion API，默认端口为 `8790`；端口被占用时会自动
从 `8791-8899` 中选择可用端口。

## 用户流程

1. 首次启动 VibeBoard Companion，在网页顶部完成 Codex Hooks 绑定和 VibeBoard 系统配对。
2. 浏览 Petdex 同步图库，在卡片上预览 `Idle / Run Right / Run Left / Waving / Jumping /
   Failed / Waiting / Running / Review` 九种原生动作。
3. 点击“部署到板子”，或从公开网站打开
   `vibeboard://pet/install?source=petdex&slug=<slug>&digest=<sha256>`。
   Companion 只在首次导航时消费该深链并立即清除本地 URL 参数；刷新页面不会重新筛选图库或
   重复部署同一宠物。
   “部署到板子”本身会自动完成下载、校验和传输，不要求用户先保存文件。次操作“保存 `.hpet`”
   只用于离线留档；未绑定 Codex 或未连接板子时，部署按钮会显示对应前置条件并禁用。
4. Companion 下载 Petdex 源资源、验证九行素材和五个板端任务状态、生成并签名 `.hpet`，再合成可信
   `codex_pet` Runtime App。
5. BLE 事务安装期间暂停任务状态发送；`install_end` 后验证宠物和 UI tick，再回放一次
   最新 Codex 快照。

本地页面：

```sh
./scripts/codex_pet_monitor.command
# 默认地址为 http://127.0.0.1:8790/；端口冲突时以服务输出的实际地址为准
```

离线预览不连接板子：

```sh
.venv/bin/python scripts/codex_pet_companion.py --port 8791
```

## `.hpet` 信任边界

`.hpet` 是严格白名单 ZIP，只允许：

```text
hpet.json
catalog.txt
preload.bin
preview.webp
signature.ed25519
```

包内不能包含 Lua、脚本、绝对路径或 `..`。Ed25519 签名覆盖规范化 manifest 和三个
payload 的大小、SHA-256。转换器只接受无凭据、标准 HTTPS 端口的 `assets.petdex.dev`；下载
过程中的每一次重定向都必须再次通过相同白名单，不能利用 CDN 跳转把 Companion 带到任意主机。
转换器支持 8x9 和 8x11、单格 192x208 的 Petdex spritesheet。Companion 图库按 Petdex 的固定
前九行完整预览；板端
`pet/v1` 仍是任务状态协议，因此五种任务状态按下表映射；其余四种由板端交互和审批状态触发：

这份行号和任务映射只有一个代码源：`scripts/petdex_state_contract.json`。Companion API、旧批量
导入器和 `.hpet` 转换器都读取该文件，禁止各自维护另一份行号表。

| 板端状态 | Petdex 状态 | 源行 |
| --- | --- | --- |
| `idle` | `idle` | 0 |
| `running` | `running` | 7 |
| `ready` | `waving` | 3 |
| `needs` | `waiting` | 6 |
| `blocked` | `failed` | 5 |

九个状态都必须找到至少两张非透明、视觉不同的帧，最多保留 8 帧。`VBPC v2` 输出固定为
160x173 RGB565A、120ms，并按状态分别保存可变帧数的 zlib 块。板端使用两个“当前状态”缓存，
最多常驻 16 帧（1328640 字节），不会一次性解压全部状态。`Run Right / Run Left` 由中间区域
左右滑触发，`Jumping` 由点击宠物和部署后首次启动触发，`Review` 用于真实审批等待。

签名前和验签后都必须解析 `VBPC v2` 状态目录：9 个状态块必须从目录末尾开始连续排列，不允许
空洞、重叠或尾随数据；每个 zlib 流必须完整结束，解压长度必须等于该状态帧数乘以固定帧字节数。
manifest 的逐状态帧数、总帧数和最大帧数还必须与该目录一致。这样损坏或元数据矛盾的转换产物
会在主机端立即失败，不会先被签名并经 BLE 传完整包后才由板子拒绝。

构建和验证：

```sh
node scripts/build_hpet_petdex.js --self-test
node scripts/codex_pet_web_test.js
.venv/bin/python scripts/hpet_package.py --self-test
.venv/bin/python scripts/hpet_package.py --verify pet.hpet --public-key public.pem
```

开发环境默认在 `~/.vibeboard/companion/keys` 生成本地 Ed25519 密钥。正式发布时必须通过
`VIBEBOARD_HPET_PRIVATE_KEY` / `VIBEBOARD_HPET_PUBLIC_KEY` 注入受控发布密钥；私钥不能进入
仓库、网页或板子。

## 本地 API

- `GET /api/pets`、`GET /api/pets/{slug}`：Petdex 同步目录。
- `POST /api/packages/{slug}`：异步构建签名 `.hpet`。
- `GET /api/packages/{digest}.hpet`：按不可变摘要下载。
- `POST /v1/session`：签发 15 分钟 capability token。
- `GET /v1/status`：Companion、Codex Hooks 和板子状态。
- `POST /v1/board/pair`、`POST /v1/codex/bind|unbind`。
- `POST /v1/pets/install`、`GET /v1/jobs/{id}`。

服务只绑定 loopback。带 Origin 的请求必须来自当前 Companion 的精确端口，或来自
`VIBEBOARD_COMPANION_ORIGINS` 明确列出的公开站点；写请求还必须使用 Bearer token，不使用
Cookie。远端只能提交已批准 Petdex slug，不能提交任意 URL。深链若带 digest，构建结果必须
完全匹配，否则安装失败。公网 HTTPS 页面调用固定 loopback 地址时，OPTIONS 只对已配置的精确
Origin 返回 Private Network Access 许可，不能用 `*` 放宽。网页服务在首次 BLE 扫描前启动，
因此板子关机、首次配对或扫描超时时也能立即显示设置和诊断页面。

## macOS App 与深链

开发版构建：

```sh
./scripts/build_codex_pet_companion_app.command
open ".local/VibeBoard Companion.app"
```

App 注册 `vibeboard://`，日志写入 `~/.vibeboard/companion/companion.log`，不会把长期服务的
stdout 留给可能关闭的 Terminal pipe。默认是 ad-hoc 签名；发布构建需要设置
`CODEX_PET_CODESIGN_IDENTITY`，并在产物流水线中完成 Developer ID 签名、公证和 stapling。
App 已把 Python 3.12 Agent、Bleak/CoreBluetooth、Node、Sharp、Hooks、网页和 Runtime 模板
打入自身；安装后的用户不需要仓库、`.venv`、Node 或终端。菜单栏提供状态、图库、重新配对、
登录启动、更新检查和诊断日志，Agent 异常退出后会有界重启。

构建会产出 `.app`、架构对应的 DMG 和 SHA-256。正式发布的域名、Developer ID、公证和更新
manifest 配置见 `docs/macos-companion-release.md`。离线包校验：

```sh
./scripts/verify_codex_pet_companion_app.sh
```

## 安装恢复

- 安装锁保证同一时间只有一个部署任务，所有 GATT 命令仍走 Bridge 的串行队列。
- 传输期间 Hooks 继续更新内存快照，但不发 BLE；成功后只回放最新一次。
- BLE 安装使用 255 字符独立上限和 96 字节分块，避免 CoreBluetooth 在板端 worker 持续文件写入时发生回压掉链；串口仍保留 250 字符
  FinSH 上限。首次刷机后的串口 provisioning 使用已 ACK 的 binary blob 模式，避免数千条文本命令。
- 中途失败时先关闭失效 transport，再重连并发送 `install_abort`。未执行 `install_end` 时旧
  App 不会被 staging 覆盖。
- `install_end` 已发送但 ACK 丢失属于“提交结果不确定”：transport 不发送 abort，Companion
  重连并用目标宠物严格 ready gate 裁决成功或失败。板端将 SD/PSRAM reload 延迟到 ACK 窗口之后。
- 已提交但板端严格校验失败时，重新连接并安装 `active.json` 记录的上一只缓存宠物。
- 连接成功不等于 Codex Pet 已启动。其他 App active 时仍允许部署；只有 `install_end` 后才
  要求 `slug/frames/frameMs/preloadedBytes/residentCompressedBytes/uiTicks/queuedFlows` 全部通过。
- 固件升级后，若 macOS 报 `Peer removed pairing information`、错误 13 或读到其他服务签名，
  必须在两端清除旧 bond 后重新系统配对；不要靠无限重试恢复不一致的密钥或 handle 缓存。
- Companion 的后台心跳和网页配对共享连接事务锁；缓存 CoreBluetooth UUID 必须先从实时广告
  解析，不能直接启动可被短超时取消的旧服务发现。网页配对会强制忽略旧缓存并写回新地址；首次配对先用受保护的 status 读取触发
  macOS 自动配对，再订阅通知。若首次 CCCD 写入仍返回错误 15，必须清理通知回调并用同一外设
  新建 Bleak client；连接 deadline 覆盖两轮扫描、服务发现和 45 秒认证窗口，默认约 170 秒。
- `install_end` 的 BLE worker 只能发布 `pending_reload`；实际 LVGL 重载由 GUI timer 消费，任何
  transport worker 都不能直接操作 LVGL timer 或对象。

## 发布验证

离线 gate：

```sh
node scripts/hpet_crypto.js --self-test
node scripts/build_hpet_petdex.js --self-test
.venv/bin/python scripts/hpet_package.py --self-test
.venv/bin/python scripts/codex_pet_companion.py --self-test
.venv/bin/python scripts/codex_pet_bridge.py --self-test
.venv/bin/python scripts/runtime_transport.py --self-test
.venv/bin/python scripts/runtime_architecture_audit.py --self-test
./scripts/verify_codex_pet_companion_app.sh
```

真机至少覆盖：首次配对、断线重连、每阶段断线、错误 ACK、重启、abort、上一宠物回滚、连续
50 次安装、九状态图库和板端逐状态预览、3 分钟 exercise soak 和 24 小时连接 soak。单次成功
必须看到 `pet=<slug>`、`preloadVersion=2`、`assetStates=9`、`frames>=2`、`frameMs=120`、
`preloadedBytes>0`、`residentCompressedBytes>0`、`uiTicks` 增长且 `queuedFlows=0`。任务语义
状态 sweep 还必须确认 `idle/waving/failed/waiting/running` 切换期间不进入 SD loader 阶段，
且稳定播放时 `loaderPhase=0`。50 次与 24 小时是发布 gate，不应用单次开发验证代替。
