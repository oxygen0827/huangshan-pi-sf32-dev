# Codex Pet Companion：Mac 开箱指南

本指南面向拿到已烧录 VibeBoard 的普通用户。无需安装 Python、Node.js、Git 或打开终端。

## 准备

- macOS 13 或更高版本，蓝牙已开启；
- 已安装并登录 Codex Desktop；
- VibeBoard 已开机并运行出厂 Runtime 固件；
- 从 `https://ldcx.tech/pet/` 下载与 Mac 架构匹配、已签名并公证的 Companion DMG。

## 安装 Companion

1. 打开 DMG，把 `VibeBoard Companion` 拖到 `Applications`。
2. 从“应用程序”启动一次，在系统提示时允许蓝牙访问。
3. 首次窗口中保留“登录时自动启动”，以后 Companion 会驻留在菜单栏。

Companion 自带全部运行环境。不要从 DMG 内长期运行，也不需要保留安装包。

## 完成连接

首次启动会自动在浏览器打开官网 Companion 页面；本地 App 保持在菜单栏运行。页面顶部依次确认：

1. `Companion` 为绿色。
2. 点击 `VibeBoard` 的“连接”，在 macOS 配对提示中允许连接。
3. 点击 `Codex` 的“绑定”。
4. 在 Codex 中打开 `/hooks`，核对 6 条 VibeBoard Companion Hook 后信任并继续。

首次安全配对可能需要约 45 秒。之后 Companion 会使用加密缓存自动重连。若固件升级后提示
配对信息不一致，应在 macOS 蓝牙设置中忽略旧 VibeBoard，再从页面重新连接。

## 部署宠物

在宠物图库选择宠物，可预览九种状态：待机、向右跑、向左跑、挥手、跳跃、失败、等待、运行和
审阅。点击“部署到板子”，等待底部进度显示“部署完成”。不要在传输中关闭板子或退出
Companion。

从官方公网图库点击部署时，浏览器会启动 Companion 或打开
`vibeboard://pet/install` 深链，再由本地加密 BLE 服务完成部署。网页不会直接持有蓝牙，也不会
读取 Codex 凭据。

## 日常使用

菜单栏的爪印图标会显示 Companion 状态，可用于：

- 打开 Companion 或宠物图库；
- 连接或更换板子；
- 开关登录时自动启动；
- 检查更新；
- 打开诊断日志。

退出菜单栏 Companion 会同时停止 BLE 连接和 Codex 状态同步。

在宠物主页从屏幕上边缘向下滑，可以打开当前 Codex task 的用量页；向上滑返回宠物页。页面显示：

- 当前 task 的累计 Token 和最近一轮新增 Token；
- 当前上下文占用，以及未缓存输入、缓存输入、输出的拆分；
- 官方 OpenAI provider 的本地价格估算；金额不是账单或账户余额；
- ChatGPT 登录可读取时的两个限额窗口和重置倒计时。

使用 API Key 时没有可查询的账户总额度，Token 和价格估算仍然正常显示，限额窗口会标记不可用。
自定义 provider 或中转站没有可靠费率时只显示 Token，不会套用官方价格。

## 出现问题

- 页面打不开：从菜单栏选择“打开 Companion”；若没有爪印图标，重新打开应用程序中的
  VibeBoard Companion。
- 板子未连接：确认板子已开机且在附近，再选择“连接或更换板子”。
- Codex 显示“等待信任”：在 Codex 中重新打开 `/hooks` 并信任全部 Companion Hook。
- 部署失败：保留板子电源，打开菜单栏的“诊断日志”，记录失败时间和页面错误信息。

开发者构建、域名、Developer ID、公证和发布 Gate 见
[`docs/macos-companion-release.md`](../docs/macos-companion-release.md)。
