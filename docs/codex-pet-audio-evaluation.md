# Codex Pet 音频、TTS 与唤醒词评估

更新时间：2026-07-15

## 当前产品边界

当前黄山派没有外接喇叭，因此声音反馈必须是可选增强：资源缺失、codec 不可用或没有物理
喇叭时，宠物 UI、录音、ASR 和 Codex 提交继续工作。默认交互仍为触摸/K2 按住说话，
常开麦克风和动态 TTS 均未默认启用。

内置提示音为项目生成的 16 kHz、单声道、16-bit PCM WAV：`listening`、`submitted`、
`needs_input`、`done`、`error`。提示音不含第三方素材，可用
`python3 scripts/generate_codex_pet_cues.py --check` 做逐字节复验。

## 录音与播放仲裁

Runtime audio 层维护 `capture_reserved`：

1. 录音启动先保留 codec，并停止、等待当前播放退出。
2. 保留期间所有 WAV 和 tone 播放统一返回 `-RT_EBUSY`。
3. 录音 worker 关闭 RX client 或捕获被清理后才释放保留。
4. `listening` 提示音先播放 100 ms，再停止并开始录音；录音期间绝不回放。

该规则位于共享音频层，因此 BLE、Lua、MSH、MCP 和原生宠物 helper 没有绕行路径。

## TTS 安全入口

动态 TTS 只能在电脑端生成，API Key 只能由电脑端 provider/环境持有。当前实现提供
`scripts/codex_pet_audio.py --validate-wav <file>`，仅接受：

- 未压缩 PCM WAV；
- 16 kHz 或 24 kHz、单声道、16-bit；
- 最长 10 秒、文件最大 512 KiB；
- 非空音频，并输出 SHA-256、采样率、帧数和时长。

由于目前没有喇叭，也没有独立于 App 安装事务的安全音频缓存协议，动态 WAV 上传保持
关闭。接上喇叭后的下一步是增加专用分块缓存，不复用会切换 active App 的安装协议；缓存
提交必须核对长度和 SHA-256，播放前再次执行同一 WAV 校验。MCP 当前只开放 5 个固定 cue，
不接受任意路径、文本 provider 配置或音频字节。

## xiaozhi-sf32 参考结论

评估基于 `78/xiaozhi-sf32` 主分支文件树提交
`1d3ef641ace47fae68227e9173647fd5db01f6f5`：

- 官方 README 明确列出关键词唤醒、AEC、流式 ASR/LLM/TTS、Opus 和设备端 MCP。
- KWS 使用 16 kHz mono RX、320 B/10 ms callback，3 个 1280 B 静态队列和 3072 B
  线程栈，模型/阈值与二进制库需要单独确认许可证和可再分发性。
- AEC 路径使用 16 kHz TXRX、WebRTC VAD mode 3 和 24→16 kHz 重采样，并实现说话期间
  VAD/打断策略。
- 网络语音使用 16 kHz Opus encoder、24 kHz decoder、60 ms 帧和 32 KiB Opus 栈，且
  依赖 PAN/UDP/云端协议；不适合整体并入当前 BLE-only Bridge。

参考链接：

- <https://github.com/78/xiaozhi-sf32>
- <https://github.com/78/xiaozhi-sf32/blob/main/app/src/kws/app_recorder_process.c>
- <https://github.com/78/xiaozhi-sf32/blob/main/app/src/xiaozhi_audio.c>

## 默认启用门禁

常开 KWS/AEC 只有在实体喇叭与量产接线到位后，采集以下 JSON 指标并通过
`python3 scripts/codex_pet_audio.py --evaluate-kws metrics.json` 才能启用：

| 指标 | 门槛 |
| --- | --- |
| 测试时长 | 至少 8 小时 |
| 唤醒语料 | 至少 200 条 |
| 误唤醒 | 不高于 0.2 次/小时 |
| 漏唤醒率 | 不高于 8% |
| 唤醒 P95 延迟 | 不高于 700 ms |
| 常开平均增量电流 | 不高于 10 mA |
| KWS 峰值额外 RAM | 不高于 128 KiB |
| AEC ERLE | 至少 15 dB |
| 播放中打断召回率 | 至少 90% |
| 隐私指示 | 常开麦克风必须有持续可见提示 |

任何一项未测或失败，默认产品路径继续使用触摸/K2 唤醒，不以“实验可运行”代替门禁通过。
