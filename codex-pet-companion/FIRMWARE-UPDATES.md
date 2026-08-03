# Codex Pet 固件更新

Codex Pet 把固件版本发现和升级权限收敛到 Companion。公网图库只是界面：它调用
loopback Companion API，由 Companion 获取发布信息、验签并独占板子传输。网页不能
持有固件私钥，也不能直接向蓝牙特征写入固件。

## 用户流程

1. 用户启动已安装的 Companion，并连接 VibeBoard。
2. 在 Codex Pet 页面点击“检查固件更新”。
3. Companion 读取官方 HTTPS 发布源，将板子上报的固件版本与最新签名版本比较。
4. 页面只会显示“已是最新”“发现更新”“发布源不可用”“板子未连接”或“板子版本未知”之一。

只有签名检查明确发现较新版本后，页面才会显示升级入口。升级始终由 Companion 执行，
不能由公网网页执行。

## 当前交付边界

量产基础 Runtime 通过 `vb_runtime_capabilities` 顶层的 `firmwareVersion` 上报发布版本。
Companion 也兼容读取 `firmware_version`、`fw` 或 `firmware.version`，但 Runtime API 版本仅代表
兼容性合同，不等于固件发布版本；`firmwareVersion` 必须与签名 release 的 `version` 完全一致。

旧板在首次迁移时没有这个字段。它可以连接并读取官方发布源，但 Companion 必须显示“当前板子
未上报固件版本”，而不能猜测它是否为最新或自动刷写。网页在本机 Companion 已识别唯一的 USB
UART 时会显示“安装基础固件”；用户必须在浏览器确认覆盖，Companion 才会安装发布源中唯一标记
`baseline: true` 的签名版本。开发板完成该迁移后，Companion 才能精确比较和显示常规升级入口。

当前固件传输方式是 `verified_usb_recovery`，不是蓝牙 DFU。只有签名检查发现更新，
页面才会显示“使用 USB 更新”。宠物或 Runtime App 的 BLE 部署不能描述为固件更新。

## 发布配置

签名后的 Companion 成品只接收公开发布配置。当前 Codex Pet 发布源是：

```text
https://ldcx.tech/static/codex-pet/firmware/releases.json
```

构建时将此地址和仓库内公开验证密钥写入 App：

```sh
VIBEBOARD_FIRMWARE_MANIFEST_URL=https://downloads.example.com/firmware/releases.json
VIBEBOARD_FIRMWARE_PUBLIC_KEY_PATH=/secure/release/firmware-public.pem
```

发布源和每一个 release archive URL 都必须是无凭据的 HTTPS。Companion 使用钉住的
公钥，在更新前校验签名 manifest 与镜像哈希；只有 Runtime 健康检查通过后才记录成功。
私钥只能保留在发布基础设施中，不能进入网页、Companion 状态目录或板子。

## 开发板验证顺序

公开发布源同时保留 `1.0.0` 基础版和 `1.0.1` 验证更新。对没有版本字段的旧开发板，先在
USB 连接状态下按已签名的 `1.0.0` release 执行一次显式基础刷写；不要绕过签名或把 BLE 宠物
部署当作固件升级。重启并重新连接后，页面应显示当前 `1.0.0`、发现 `1.0.1`，此时才可以通过
Companion 的“使用 USB 更新”完成 1.0.1 验证。

```sh
.venv/bin/python scripts/firmware_release.py apply \
  --release .local/firmware-releases/1.0.0 \
  --port /dev/cu.usbserial-XXXX \
  --board sf32lb52-lchspi-ulp \
  --public-key codex-pet-companion/keys/firmware-public.pem \
  --confirm UPDATE_FIRMWARE
```

## 发布归档

不要用 Finder 压缩或 macOS `ditto` 直接制作固件 ZIP。这些工具可能写入 `._*` 或
`.DS_Store` 元数据，导致同一发布内容在不同平台上的哈希不一致。签名 release 目录验证通过
后，只能通过下列命令生成可上传的归档；命令会拒绝覆盖文件、固定 ZIP 元数据、重新解压验签，
并输出要写入发布源的 SHA-256。

```sh
.venv/bin/python scripts/firmware_release.py archive \
  --release .local/firmware-releases/<version> \
  --output .local/firmware-releases/VibeBoard-Firmware-<version>-sf32lb52-lchspi-ulp.zip \
  --public-key codex-pet-companion/keys/firmware-public.pem
```

## 未来蓝牙 DFU

蓝牙固件更新属于独立的黄山派平台发布，不是 Codex Pet 网页功能。启用前，板端平台必须
完成并验证 A/B Bootloader 分区、启动链签名验镜像、断连续传、电量/供电检查、失败启动回滚
和真实断电测试。届时 Companion 才能在现有签名检查流程后增加蓝牙传输。此前页面必须继续
明确显示“USB 恢复更新”。
