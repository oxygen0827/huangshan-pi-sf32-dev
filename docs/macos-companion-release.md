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

首次启动会先启动本地 Agent，再打开设置页。用户可选择登录时自动启动。菜单栏可查看板子与
Codex 状态、打开图库、重新配对、检查更新和打开诊断日志。

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

部署公网 HTML 时，把 `codex_pet_web.html` 的
`vibeboard-companion-download` meta content 设置为正式 DMG 的 HTTPS 下载地址。未检测到本地
服务时，页面会显示“启动 Companion”和“下载 Mac 版”。

## 构建环境

当前产物按构建机架构输出。Apple Silicon 在 arm64 runner 构建，Intel 在 x86_64 runner
构建；两个架构必须分别完成校验和公证。

```sh
python3.12 -m venv .venv
.venv/bin/pip install -r scripts/companion-requirements.txt
npm install --prefix scripts/companion_node --omit=dev
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

正式构建：

```sh
CODEX_PET_CODESIGN_IDENTITY="Developer ID Application: Example Company (TEAMID)" \
CODEX_PET_NOTARY_PROFILE="VibeBoardNotary" \
VIBEBOARD_APP_VERSION="1.0.0" \
VIBEBOARD_BUILD_NUMBER="1" \
VIBEBOARD_PUBLIC_SITE_URL="https://pets.example.com" \
VIBEBOARD_UPDATE_MANIFEST_URL="https://downloads.example.com/macos/update.json" \
VIBEBOARD_RELEASE_DOWNLOAD_URL="https://downloads.example.com/macos/VibeBoard-Companion-1.0.0-macOS-arm64.dmg" \
VIBEBOARD_FIRMWARE_MANIFEST_URL="https://downloads.example.com/firmware/releases.json" \
VIBEBOARD_FIRMWARE_PUBLIC_KEY_PATH="/secure/release/firmware-public.pem" \
VIBEBOARD_SFTOOL_PATH="$HOME/.sifli/tools/sftool/0.1.16/sftool" \
./scripts/build_codex_pet_companion_app.command
```

脚本按 inside-out 顺序签名 Node、Sharp、Python framework、PyObjC、Agent、helper、主程序和
App，创建 DMG，提交 `notarytool --wait`，staple DMG，并生成 SHA-256 与更新 manifest。

## 发布 Gate

每个公开产物必须满足：

1. `verify_codex_pet_companion_app.sh` 全部通过，包括 CoreBluetooth 导入、Hook、Bridge、九状态
   `.hpet`、Ed25519 和 Sharp 转换器测试。
2. 从 DMG 拖入 `/Applications`，在一台没有仓库、Python 和 Node 的干净 Mac 上首次启动。
3. 验证蓝牙权限、首次配对、重启自动重连、Codex Hook 绑定与信任、登录启动和深链。
4. 从配置的公网 Origin 完成一次部署，并确认其他 Origin 的 CORS/PNA 请求被拒绝。
5. `spctl`、公证和 stapling 校验通过，再上传 DMG、`.sha256` 和 `update-manifest.json`。
6. 用签名固件 feed 在 USB 板上完成 update、Runtime health、last-good 和 rollback；未完成
   双分区迁移前确认 `/v1/firmware/status` 仍返回 `wirelessDfu=false`。
7. 在页面导出诊断包，确认下载需要 session token、任务与 ZIP 一一对应，且日志中没有 token、
   password、private key、Authorization/Cookie 凭据或 Hook command；并发导出时两个任务必须拿到不同
   的 ZIP。

Developer ID 证书、公证钥匙串、正式域名和下载 URL 属于发布凭据，不能提交到仓库。
