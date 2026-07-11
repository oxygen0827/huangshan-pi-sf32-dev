# 黄山派 Runtime 高阶能力评估

更新时间：2026-07-10

本文评估从 `vibeboard-runtime-gpl` 迁移完整 Lua VM、native/NES、camera、
gamepad 等高阶能力的时机。当前结论不是“永不做”，而是：在 Runtime 包格式、
权限模型、Web/iOS 管理、长稳和恢复 gate 没有完全稳定之前，这些能力不进入
黄山派 Runtime profile。

## 当前总决策

```text
状态：暂缓
默认 profile：huangshan-pi
允许替代：受控 Lua subset、Runtime helper、host/iOS/Web bridge 注入
禁止默认 manifest 能力：native、nes、camera、gamepad、i2s、audio、wifi、http、network
```

普通 App 包、AI plan writer、Web 导入和 iOS 导入都必须继续走同一套
`runtime_package.py` / `RuntimePackage.swift` 校验。任何高阶能力进入实验阶段前，
必须先改 manifest capability schema、文档、Python/Swift parity、自测、真机回归
和恢复流程。

## 评估矩阵

| 能力 | 当前结论 | 主要风险 | 当前替代方案 | 进入实现前的最低门槛 |
| --- | --- | --- | --- | --- |
| 完整 Lua VM / 完整 LVGL binding | 暂缓 | RAM/栈不可控、长脚本卡 UI、对象生命周期和崩溃隔离不足、AI 生成代码难审计 | 受控 Lua subset + 白名单 helper；复杂交互写 Runtime 原生 helper | 脚本超时/对象配额/错误隔离设计；至少 24h 长稳；崩溃后自动恢复；包权限模型 |
| Native module ABI | 暂缓 | ABI 兼容、内存越界、固件/模块版本不匹配、签名和回滚缺失 | 固件内置 helper；host bridge 提供联网/AI/文件转换 | 模块签名、ABI 版本、沙箱边界、崩溃恢复、发布/回滚策略 |
| NES / emulator | 暂缓 | 显示接管、帧率/功耗、输入延迟、ROM 版权、音频输出、资源包体积 | `vibe_2048_game`、`vibe_snake_autoplay` 这类内置游戏 helper | 先完成 gamepad JSON、音频输出、资源许可策略、性能预算和退出恢复 |
| Camera | 暂缓 | 当前硬件路径未验证、带宽/内存压力、隐私提示、照片存储和权限 | 手机/桌面拍照后通过 bridge 发送结果或文本描述 | 明确摄像头硬件、驱动、权限 UI、隐私说明、存储配额和失败降级 |
| Gamepad | 暂缓 | BLE HID/配对复杂、输入归一化、焦点与系统手势冲突、测试矩阵大 | 触摸、KEY1/KEY2、`vb_runtime_touch`、`vb_runtime_gpio` | 先定义只读 gamepad JSON 和 helper，再接真实手柄发现/配对 |
| I2S / audio out | 暂缓 | 播放链路未完整验证、音量/功放状态、实时性和功耗 | 语音输入 bridge、host/iOS 播放或文本回写 | loopback smoke、音量 API、设备状态 JSON、长时播放稳定性 |

## 必须先完成的基础 gate

- Runtime package integrity 和恢复流程通过串口/BLE 长稳。
- Web/iOS 管理器能清楚展示安装失败、包校验失败、能力不匹配和恢复建议。
- Manifest permissions 从“能力声明”升级到“用户可理解的授权文案”。
- Python 和 Swift 包校验保持 parity。
- 高阶能力必须有单独实验 profile，不能混入默认 `huangshan-pi` profile。
- 每项能力必须有真机 smoke、失败注入和回滚路径。

## 当前执行规则

- `capabilities`、`requires`、`permissions` 中出现 `native`、`nes`、`camera`、
  `gamepad`、`i2s`、`audio`、`wifi`、`http`、`network` 时，默认打包失败。
- AI App Plan Writer 不能生成上述能力，也不能通过 `files[]` 绕过 manifest 校验。
- Web App Store 和 iOS import 只接受通过同一套 Huangshan validator 的包。
- 需要复杂 UI 或游戏时，先新增小而明确的 Runtime helper，并同步 Python/Swift
  白名单和文档。

## 下一次重新评估触发条件

满足任一条件时可以重新评估，而不是直接实现：

- 有真实产品 App 必须依赖该能力，且 host bridge 无法满足。
- 长稳和恢复 gate 已覆盖安装、启动、停止、删除、掉电、失败清理。
- Web/iOS 已能解释能力权限和失败原因。
- 有明确硬件版本和资源预算，且测试矩阵可控。
