# VibeBoard Companion macOS 发布

## 用户拿到什么

用户只安装 `VibeBoard Companion.app`，不安装 Python、Node.js，不克隆本仓库，也不打开终端。
App 是菜单栏常驻工具，内部包含：

- 原生 AppKit 外壳和 `vibeboard://` 深链；
- PyInstaller 打包的 Python 3.12 Agent、Bleak、CoreBluetooth 与 PyObjC；
- Node.js、Sharp、Petdex 九状态转换器和 Ed25519 工具；
- Codex Hook 入口、Companion 网页和 `codex_pet` Runtime App 模板。

Companion 状态目录还保存受限的任务 journal（`jobs.json`）和 `.hpet` 缓存。任务状态采用原子
替换写入；服务重启时，所有未完成任务会标记为 `interrupted`/失败，不会伪装成仍在传输。缓存
默认最多保留 24 个包、128 MiB，并保护当前包与回滚所需的上一个包。网页可读取
`GET /v1/health`，其中 `serviceReady` 表示本地服务和资源配置完整，`boardReady` 还要求板子已
连接；部署按钮必须同时满足这两个边界。

首次启动会先启动本地 Agent，确认本地 API 就绪后打开公网 `https://ldcx.tech/pet/` 设置页。网页
负责界面，Companion 不嵌入网页；它只保留 BLE、Codex Hook、签名固件更新、本地 API 和菜单栏
状态。用户可选择登录时自动启动。菜单栏可查看板子与 Codex 状态、打开图库、重新配对、检查
更新和打开诊断日志。

## 公网站点联动

公网域名必须在构建时写入，不能使用通配 Origin：

```sh
export VIBEBOARD_PUBLIC_SITE_URL="https://pets.example.com"
export VIBEBOARD_UPDATE_MANIFEST_URL="https://downloads.example.com/macos/update.json"
export VIBEBOARD_RELEASE_DOWNLOAD_URL="https://downloads.example.com/macos/VibeBoard-Companion.dmg"
export VIBEBOARD_FIRMWARE_MANIFEST_URL="https://downloads.example.com/firmware/releases.json"
export VIBEBOARD_FIRMWARE_PUBLIC_KEY_PATH="/secure/release/firmware-public.pem"
export VIBEBOARD_SFTOOL_PATH="$HOME/.sifli/tools/sftool/0.1.16/sftool"
```

App 只把 `VIBEBOARD_PUBLIC_SITE_URL` 的精确 Origin 加入本地 API 白名单。公网网页访问固定的
`http://127.0.0.1:8790`；服务仍只监听 loopback，并响应浏览器的 Private Network Access
预检。写操作还需要 15 分钟 capability token。公开网页可用
`vibeboard://companion/open` 启动 Companion，用
`vibeboard://pet/install?source=petdex&slug=<slug>&digest=<sha256>` 交接部署。

固件 manifest URL、公钥和 `sftool` 在构建时写入成品资源。私钥绝不能放进 App。Companion
只接受 HTTPS feed 和钉住公钥验证通过的 release；USB 启动日志和 Runtime health 都通过后才
记录 last-good，失败会尝试刷回上一个签名 release。当前生产分区仍是单槽，网页必须显示
`wirelessDfu=false`，不能把宠物 BLE 部署冒充为固件 DFU。

仓库中的 `scripts/codex_pet_web.html` 故意保留空的
`vibeboard-companion-download`，避免开发包或未公证 DMG 被 C 端用户下载。正式发布脚本只有在
签名、公证、包内自检都通过后，才生成 `.local/dist/codex_pet_web.html` 并注入与更新 manifest
完全一致的 HTTPS 地址。未检测到本地服务时，正式页面会显示“启动 Companion”和“下载 Mac
版”。不要手工修改或上传源码页来绕过这个 Gate。

## 构建环境

当前产物按构建机架构输出。Apple Silicon 在 arm64 runner 构建，Intel 在 x86_64 runner
构建；两个架构必须分别完成校验和公证。

```sh
python3.12 -m venv .venv
.venv/bin/pip install -r scripts/companion-requirements.txt
npm install --prefix scripts/companion_node --omit=dev --include=optional --package-lock
```

开发构建：

```sh
./scripts/build_codex_pet_companion_app.command
./scripts/verify_codex_pet_companion_app.sh
```

输出：

```text
.local/VibeBoard Companion.app
.local/dist/VibeBoard-Companion-<version>-macOS-<arch>.dmg
.local/dist/VibeBoard-Companion-<version>-macOS-<arch>.dmg.sha256
```

开发构建使用 ad-hoc 签名，不可直接公开分发。在 npm 不可用时，它可借用本机 ChatGPT 的
Sharp 做本地构建；只要设置正式签名身份，构建脚本就会禁止这个回退并要求项目自己的依赖。

## 签名与公证

先把 App Store Connect 或 Developer ID 公证凭据保存到钥匙串：

```sh
xcrun notarytool store-credentials VibeBoardNotary
```

正式构建前，构建机钥匙串必须存在 `Developer ID Application` 证书，并保存公证凭据：

```sh
CODEX_PET_CODESIGN_IDENTITY="Developer ID Application: Example Company (TEAMID)" \
CODEX_PET_NOTARY_PROFILE="VibeBoardNotary" \
VIBEBOARD_APP_VERSION="1.0.0" \
VIBEBOARD_BUILD_NUMBER="8" \
./scripts/release_codex_pet_companion_macos.command
```

脚本按 inside-out 顺序签名 Node、Sharp、Python framework、PyObjC、Agent、helper、主程序和
App，创建 DMG，提交 `notarytool --wait`，staple DMG，并生成 SHA-256、更新 manifest 和匹配的
公网 HTML。版本号和 build 必须显式提供；同一版本的紧急修复也必须递增 build。

默认正式端点已经固定为当前生产环境，云服务器是用户更新主源：

```text
站点：https://ldcx.tech/pet/
Companion：https://ldcx.tech/static/codex-pet/companion/
更新清单：https://ldcx.tech/static/codex-pet/companion/update-manifest.json
固件清单：https://ldcx.tech/static/codex-pet/firmware/releases.json
```

GitHub Release 只作为可选的代码归档和灾备镜像，不是用户的主下载源。需要镜像时设置
`VIBEBOARD_PUBLISH_GITHUB_RELEASE=1`，否则发布脚本只更新云服务器静态目录。

菜单栏“检查更新”读取服务器 manifest；发现新 build 后显示“更新”，下载并打开对应 DMG。macOS
安装包仍需要用户把新版本拖入“应用程序”完成替换，不能由普通 App 在后台直接覆盖自身。

先进行只读发布校验：

```sh
VIBEBOARD_APP_VERSION="1.0.0" \
VIBEBOARD_BUILD_NUMBER="8" \
./scripts/publish_codex_pet_companion_macos.sh --dry-run
```

确认后上传。脚本会先上传到服务器临时目录、远端复核 SHA-256，然后备份旧文件并依次发布
版本化 DMG、校验和、更新清单和匹配网页。它会拒绝未 staple 或未通过 Gatekeeper 的 DMG：

```sh
VIBEBOARD_APP_VERSION="1.0.0" \
VIBEBOARD_BUILD_NUMBER="8" \
./scripts/publish_codex_pet_companion_macos.sh
```

服务器密码、Developer ID 私钥和公证凭据都只保存在系统凭据设施中，不写入脚本或仓库。

## 发布 Gate

每个公开产物必须满足：

1. `verify_codex_pet_companion_app.sh` 全部通过，包括 CoreBluetooth 导入、Hook、Bridge、九状态
   `.hpet`、Ed25519 和 Sharp 转换器测试。
2. 从 DMG 拖入 `/Applications`，在一台没有仓库、Python 和 Node 的干净 Mac 上首次启动。
3. 验证蓝牙权限、首次配对、重启自动重连、Codex Hook 绑定与信任、登录启动和深链。
4. 从配置的公网 Origin 完成一次部署，并确认其他 Origin 的 CORS/PNA 请求被拒绝。
5. `spctl`、公证和 stapling 校验通过，再通过正式发布脚本上传 DMG、`.sha256`、
   `update-manifest.json` 和匹配网页；禁止单独更新其中一个文件。
6. 用签名固件 feed 在 USB 板上完成 update、Runtime health、last-good 和 rollback；未完成
   双分区迁移前确认 `/v1/firmware/status` 仍返回 `wirelessDfu=false`。
7. 在页面导出诊断包，确认下载需要 session token、任务与 ZIP 一一对应，且日志中没有 token、
   password、private key、Authorization/Cookie 凭据或 Hook command；并发导出时两个任务必须拿到不同
   的 ZIP。

Developer ID 证书和公证钥匙串属于发布凭据，不能提交到仓库。正式构建必须使用项目锁定的
Node 依赖并通过 `npm audit --omit=dev`；不得使用开发构建允许的本机 ChatGPT Sharp 回退。
