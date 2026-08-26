# 脚本导航

`scripts/` 保持扁平，是为了让烧录、打包、PyInstaller 和 macOS App 内置资源使用稳定的相对路径。
不要只为分类移动已有入口；新增脚本应放在与其调用方相邻的位置，并在本页登记。

| 范围 | 主要入口 |
| --- | --- |
| 构建与刷机 | `build.sh`、`build.ps1`、`flash.py`、`flash.sh`、`monitor.sh` |
| Runtime 打包与可靠性 | `runtime_package.py`、`runtime_transport.py`、`runtime_deep_check.py`、`runtime_recovery_soak.py`、`runtime_architecture_audit.py` |
| Codex Pet Companion | `codex_pet_companion.py`、`codex_pet_web.html`、`codex_pet_appserver.py`、`codex_pet_bridge.py`、`codex_pet_hook.py` |
| Companion 状态与缓存 | `companion_state.py`（任务 journal、重启恢复、`.hpet` 缓存上限与健康边界） |
| 宠物资源与 `.hpet` | `import_petdex_pets.js`、`build_hpet_petdex.js`、`hpet_package.py`、`hpet_crypto.js` |
| 固件发布与恢复 | `firmware_release.py`、`companion_firmware.py`、`dual_bank_dfu.py`、`companion_diagnostics.py` |
| 语音桥 | `voice_bridge_common.py`、`voice_bridge_serial.py`、`voice_bridge_ble.py`、`voice_llm_openai.py`、`voice_llm_zhipu.py` |
| macOS 打包 | `build_codex_pet_companion_app.command`、`verify_codex_pet_companion_app.sh`、`codex_pet_companion_app.swift` |

`runtime_architecture_audit.py` 还负责锁定 Codex Pet 的动画存储边界：五个任务语义状态必须从
PSRAM 常驻压缩块加载，resident loader 不得调用 SD/FAT 或 storage mutex；交互预览状态才允许
使用有界流式读取。修改 `vb_runtime_codex_pet.c` 的预载、loader 或状态 JSON 时必须运行该审计。

可提交的源码在本目录和 `scripts/runtime_apps/`。`.local/`、`project/build_*`、Python virtualenv、
Node 依赖、转换后的宠物素材和设备抓包均为本机生成物，已由根目录 `.gitignore` 排除。
