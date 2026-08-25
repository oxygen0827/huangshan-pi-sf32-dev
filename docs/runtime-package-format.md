# 黄山派 Runtime App 包格式

更新时间：2026-07-11

黄山派 Runtime App 是普通文件目录，安装到：

```text
/sdcard/apps/<app_id>
```

安装过程先写入 staging 目录，`vb_runtime_install_end <app_id>` 提交前会做完整性
校验。提交失败时保留旧 App。

## 最小目录

```text
my_app/
  main.lua
  manifest.json
  assets/...
```

旧兼容包也可以使用：

```text
legacy_app/
  main.lua
  app.info
```

`app.info` 只保留给兼容和快速调试；正式包应使用 `manifest.json`。

## manifest v1

```json
{
  "schemaVersion": 1,
  "kind": "huangshan-runtime-app-manifest",
  "id": "my_app",
  "entry": "main.lua",
  "runtimeProfile": "huangshan-pi",
  "capabilities": ["display", "touch"],
  "components": []
}
```

最低要求：

- `schemaVersion` 或 `version` 必须为整数 `1`。
- `kind` 必须是 `huangshan-runtime-app-manifest`。
- `id` 必须等于安装 App ID。
- `entry` 必须是 `main.lua`。
- `runtimeProfile` / `targetProfile` / `target` 如果存在，必须指向 Huangshan
  profile。

## 路径规则

允许路径：

- `manifest.json`
- `app.info`
- `main.lua`
- `files.txt`
- `README.md`
- `assets/`、`images/`、`fonts/`、`lib/` 下的 `json/txt/png/jpg/jpeg/bin/ttf/otf/lua/wav`

禁止：

- 绝对路径。
- `..`
- 连续斜杠。
- 非白名单目录或扩展名。

## Lua 与音频边界

- `main.lua` 和 App 本地 `lib/*.lua` 使用 Lua 5.5 完整语言语法。
- Runtime 只开放 base/coroutine/table/string/math/utf8，以及受控的 App 本地
  `require`/`dofile`/`loadfile`；不开放 `os`、`io`、`debug`、`package` 和动态 C 模块。
- 单个脚本最大 64 KiB；VM 默认限制 384 KiB 内存和 50 万条指令。
- `audio` / `audio.playback` 包可播放自身 `assets/*.wav`：PCM、16-bit、8-48 kHz、
  mono/stereo。App 不获得任意文件或直接 I2S 访问权。

## 完整性字段

主机打包器和 iOS `RuntimePackage` 会自动补齐：

```json
{
  "files": [
    {
      "path": "main.lua",
      "size": 12,
      "sha256": "..."
    },
    {
      "path": "assets/note.txt",
      "size": 5,
      "sha256": "..."
    }
  ],
  "integrity": {
    "algorithm": "sha256",
    "filesDigest": "..."
  }
}
```

规则：

- `files[]` 列出除 `manifest.json` 自身以外的包内文件。
- 每项包含 `path`、`size` 和 64 位十六进制 SHA-256。
- 如果 manifest 声明了 `files[]`，板端提交前会逐个读取 staging 文件并校验大小和 SHA-256。
- `files[]` 必须至少包含 `main.lua`。
- `integrity.filesDigest` 与 GPL 项目保持兼容，是 canonical `files[]` JSON 加换行后的 SHA-256；当前黄山派板端先不强制校验这个聚合 digest。
- 没有 `files[]` 的旧 manifest 仍可安装，但不会获得逐文件 hash gate。

## 推荐命令

离线校验所有示例包：

```bash
python3 scripts/runtime_package.py --all
```

校验单个包：

```bash
python3 scripts/runtime_package.py --package-dir scripts/runtime_apps/display_stage
```

包校验自测：

```bash
python3 scripts/runtime_package.py --self-test
```

安装链路会使用校验后、已补齐 integrity 的 manifest；不要手写安装命令绕过打包器，
除非正在做底层调试。
