# Codex Pet Companion — 开箱指南

从开机到宠物出现在板子上、并跟 Codex Desktop 连通，大约需要 10 分钟。

---

## 前置条件

| 需要 | 说明 |
|---|---|
| 立创黄山派 | 已烧录含 Codex Pet 的 VibeBoard Runtime 固件 |
| macOS | 蓝牙开启；推荐 macOS 13 及以上 |
| ChatGPT.app | 已安装 Codex Desktop（`/Applications/ChatGPT.app`） |
| Python 3.11+ | 项目 `.venv` 已安装依赖 |
| Node.js 18+ | 用于生成宠物帧资源（首次需要） |

首次准备 Python 环境（只需做一次）：

```bash
python3 -m venv .venv
.venv/bin/python -m pip install bleak
```

---

## 第一步：生成宠物帧资源

板端宠物的动画帧需要在本机生成（原始素材版权归Petdex 各作者，不随代码一起提交）。

```bash
node scripts/import_petdex_pets.js
```

这会从 `petdex.dev` 下载 Shinchan（默认 active 宠物）的 spritesheet，转换为 160×173 的 RGB565 帧包，并生成压缩后的 `preload.bin`。

验证生成结果：

```bash
node scripts/import_petdex_pets.js --check
```

如需换成其他宠物（如 Boxcat）：

```bash
CODEX_PET_ACTIVE_SLUG=boxcat node scripts/import_petdex_pets.js
```

---

## 第二步：把 App 安装到板子

将 Codex Pet App（含宠物帧）通过串口推到板子的 TF 卡：

```bash
.venv/bin/python scripts/runtime_install_serial.py /dev/cu.usbserial-XXXX \
  --package-dir scripts/runtime_apps/codex_pet \
  --binary-install \
  --no-echo \
  --final-wait 3 \
  --ready-timeout 90
```

或者使用一键脚本（首次新板，自动完成固件烧录 + App 安装）：

```bash
bash scripts/provision_codex_pet_board.sh /dev/cu.usbserial-XXXX
```

安装完成后重启板子，串口日志应出现：

```
[vb_runtime][codex_pet] preloaded pets=1 states=5 frames=2 bytes=830400
[vb_runtime][lua] codex.pet rc=0
```

---

## 第三步：启动 Bridge 服务

在项目根目录运行：

```bash
./scripts/codex_pet_monitor.command
```

终端会显示：

```
══  VibeBoard Codex Pet 监控  ══════════════════════════
服务启动后请用浏览器打开以下地址完成配对与宠物部署：

    http://127.0.0.1:8790/

板子开机后蓝牙会自动扫描 VibeBoard，连接可能需要 30～60 秒。
按 Ctrl-C 停止服务。
════════════════════════════════════════════════════════
```

**保持这个终端窗口打开。** Bridge 进程必须持续运行，Codex Desktop 的状态才能同步到板子。

---

## 第四步：连接板子（BLE 配对）

打开浏览器，访问 **http://127.0.0.1:8790/**

页面顶部有三个连接状态指示灯：

```
● Companion   ● Codex   ● VibeBoard
```

1. **Companion** 应为绿色（服务正常）
2. 点击 **VibeBoard** 旁边的"连接"按钮
3. macOS 会弹出蓝牙配对请求，点击允许
4. 等待约30～60 秒，VibeBoard 指示灯变绿

> 首次配对说明：macOS CoreBluetooth 在首次连接时需要完成安全握手，可能等待约 45 秒。此后重启 Bridge 会自动重连缓存的设备，通常 10 秒内完成。

---

## 第五步：注册 Codex Hooks

点击页面上 **Codex** 旁边的"绑定"按钮。

这会将 `codex_pet_hook.py` 注册到 `~/.codex/hooks.json`，覆盖以下6个事件：
- `SessionStart` — Codex 会话打开
- `UserPromptSubmit` — 你提交任务
- `PreToolUse / PostToolUse` — 每次工具调用前后
- `PermissionRequest` — 需要审批时
- `Stop` — 任务结束

**绑定是一次性操作**，之后每次启动 Bridge 均自动生效。

---

## 第六步：配置 MCP 工具（可选）

MCP 工具让 Codex 本身能够主动查询和控制硬件（如显示消息、切换宠物、播放提示音）。

查看配置模板：

```bash
cat docs/codex-pet-mcp.toml
```

模板里已经列出了可用工具（`huangshan_status`、`huangshan_pet_status`、`huangshan_show_message` 等）。
将其中的内容合并到 Codex Desktop 的 `~/.codex/config.toml` 即可启用：

```bash
# 查看模板内容后，手动合并到 ~/.codex/config.toml
cat docs/codex-pet-mcp.toml
```

> 注意：MCP 工具配置的绝对路径（解释器和脚本路径）需要与你本机的仓库位置一致。`.codex/config.toml` 里已包含本机路径的版本，可直接参考。

---

## 完整开箱完成

这时板子上应该出现宠物，RGB 灯亮起（默认蓝色待机）。

打开 Codex Desktop 提交一个任务，观察：
- 提交时 → 宠物进入"运行"动画，RGB 变色
- 工具调用中 → 动画持续
- 任务完成 → 宠物进入"完成"动画，提示音播放
- 遇到需要审批的操作 → 宠物变为"等待"动画，板子左键=Deny、右键=Allow

---

## 换宠物

宠物可以随时替换，无需重新烧录固件。

**方式一：命令行**

```bash
CODEX_PET_ACTIVE_SLUG=boxcat node scripts/import_petdex_pets.js
.venv/bin/python scripts/runtime_install_serial.py /dev/cu.usbserial-XXXX \
  --package-dir scripts/runtime_apps/codex_pet --binary-install --no-echo
```

**方式二：Companion 网页（8790）**

1. 在宠物图库里选择目标宠物
2. 点击"部署到板子"
3. 等待下载、转换、BLE 传输完成（进度条）

---

## 日常工作流

每天：

```
1. 开发板开机
2. 运行 ./scripts/codex_pet_monitor.command（保持终端打开）
3. 正常使用 Codex Desktop 工作
4. 眼角余光看板子感知 AI 状态
5. 需要审批时，按右键 Allow 或左键 Deny
```

结束后 `Ctrl-C` 停止 Bridge，板子 30 秒内自动进入离线状态。

---

## 排障

### 板子连接不上

1. 确认板子已开机（屏幕亮起）
2. 点击网页"连接"，等待最多 60 秒
3. 检查 Bridge 终端有无报错，日志保存在 `.local/codex_pet_monitor.log`
4. 如果 macOS 蓝牙状态异常，可在终端手动扫描验证：
   ```bash
   .venv/bin/python scripts/runtime_install_ble.sh --status-only
   ```

### Codex 状态不同步

- 确认 Hooks 已绑定（网页 Codex 指示灯绿色）
- **Bridge 必须先于 Codex Desktop 会话启动** — 若先开了 Codex 再启动 Bridge，当次会话的事件不会补发，重新打开一个新对话即可
- 重新绑定：点击"解绑"再点"绑定"

### Bridge 进程存在但板子显示离线

这是已记录的已知故障模式（详见 `docs/codex-pet-bridge.md` 事故复盘）。
诊断命令：

```bash
ps -axo pid,ppid,stat,command | grep codex_pet_bridge
```

检查 Bridge 是否由正确的终端持有。如果进程父进程已不存在，用 `SIGTERM` 终止旧进程后重启：

```bash
pkill -f "codex_pet_bridge.py --mode monitor"
./scripts/codex_pet_monitor.command
```

### 宠物帧未加载

串口日志没有出现 `preloaded pets=1 states=5 frames=2 bytes=830400`，检查：

1. `node scripts/import_petdex_pets.js --check` 验证帧资源完整性
2. TF 卡存在 `/sdcard/apps/codex_pet/assets/pets/preload.bin`
3. PSRAM 是否成功分配（日志中检查内存分配失败提示）

---

## 相关文档

- [README.md](README.md) — 产品概述、架构与文件索引
- [../docs/codex-pet-bridge.md](../docs/codex-pet-bridge.md) — 协议、状态归并、事故复盘与排障清单
- [../docs/codex-pet-one-click-deploy.md](../docs/codex-pet-one-click-deploy.md) — 宠物部署架构与 `.hpet` 格式说明
