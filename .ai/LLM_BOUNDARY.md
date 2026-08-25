# LLM 修改边界

> 本文件与 `.ai/LLM_RULES.md` 配套使用。每次执行前重新读取。

## 项目类型

SiFli SDK + RT-Thread + LVGL + SCons 的固件工程（无代码生成器）。

## 绝对禁止修改

```text
$SIFLI_SDK_PATH/                        (厂商 SDK，只读)
hardware/                               (板级硬件/BSP)
src/third_party/lua/                    (Lua VM 既有源码，只读)
src/drivers/                            (外设驱动，只读)
src/resource/                           (资源与图片，只读)
启动文件 / 链接脚本（startup_*.s、*.ld、*.icf 等）
```

## 默认允许修改

```text
src/gui_apps/VibeBoard_Runtime/         (VibeBoard Runtime 应用层)
scripts/                                (构建/烧录/回归脚本；仅修复必需时)
docs/                                   (文档)
.ai/                                    (本边界文件)
scripts/runtime_apps/                   (Runtime Lua 应用)
```

## 需要用户单独确认后才能修改

```text
src/gui_apps/VibeBoard_Runtime/main.c   (Runtime 主入口/状态机，需逐轮确认)
src/gui_apps/VibeBoard_Runtime/vb_runtime_lua.c
src/gui_apps/VibeBoard_Runtime/vb_runtime_lua_host.h
project/Kconfig、project/proj.conf 等构建配置（一般只读）
```

## 已验证代码与时序敏感位置

```text
Lua close / host stop / watchdog 恢复路径（问题三十六区域）
vibeboard_lua_stop_app() 同步关闭路径
vb_timer_cb 的 reload/pending_reload 分支
on_start() 启动顺序（watchdog 与 vb_load_active_package）
```

## 主入口和主循环保护点

```text
on_start()/on_stop() 不删除既有调用，不重排既有调用
vb_timer_cb 不改变既有 LVGL 定时器语义，只在其分支内做有界恢复
```

## 测试与生成物

```text
/tmp 下调试脚本为临时物，验证后清理
project/build_* 为构建输出，不入库
```
