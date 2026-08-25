# 文档导航

本目录保留已验证的产品契约、板端边界和发布流程；不把构建产物、私钥、诊断包或设备日志提交到这里。

| 主题 | 文档 |
| --- | --- |
| Runtime App 包、能力和边界 | `runtime-package-format.md`、`runtime-capabilities.md`、`runtime-boundary.md` |
| 板端开发与历史问题 | `runtime-app-development-notes.md`、`board-bringup.md`、`runtime-app-plan-writer.md` |
| Codex Pet 与 Companion | `codex-pet-one-click-deploy.md`、`codex-pet-bridge.md`、`macos-companion-release.md` |
| 固件发布、恢复和诊断 | `firmware-release-and-recovery.md` |
| 硬件基线、屏幕、传感器与网络边界 | `sf32lb52-module-hardware-reference.md`、`board-app-separation.md`、`huangshan-networking.md`、`runtime-high-risk-capabilities-evaluation.md` |
| 上游与 SDK 调研 | `upstream.md`、`sifli-sdk-map.md`、`sifli-learning-path.md` |

新增设计决策或真实设备故障时，先更新最贴近主题的契约文档，再把现象、根因、修复和回归要求补进
`runtime-app-development-notes.md`。这样发布说明和开发经验不会漂移。
