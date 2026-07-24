#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

CHECKS: list[tuple[str, str]] = []


def fail(name: str, message: str) -> None:
    CHECKS.append(("fail", f"{name}: {message}"))


def ok(name: str, message: str) -> None:
    CHECKS.append(("ok", f"{name}: {message}"))


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def python_functions(rel: str) -> set[str]:
    tree = ast.parse(read(rel), filename=rel)
    return {node.name for node in ast.walk(tree) if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))}


def require_contains(name: str, rel: str, needle: str) -> None:
    text = read(rel)
    if needle not in text:
        fail(name, f"{rel} missing {needle!r}")
    else:
        ok(name, f"{rel} contains {needle!r}")


def require_file(name: str, rel: str) -> None:
    if not (ROOT / rel).is_file():
        fail(name, f"{rel} is missing")
    else:
        ok(name, f"{rel} exists")


def require_absent(name: str, rel: str, pattern: str, *, flags: int = 0) -> None:
    text = read(rel)
    if re.search(pattern, text, flags):
        fail(name, f"{rel} still matches forbidden pattern {pattern!r}")
    else:
        ok(name, f"{rel} has no forbidden pattern {pattern!r}")


def audit_runtime_transport() -> None:
    rel = "scripts/runtime_transport.py"
    text = read(rel)
    functions = python_functions(rel)
    required = {
        "SyncRuntimeTransport",
        "AsyncRuntimeTransport",
        "SerialTransport",
        "BLETransport",
        "combine_app_pages",
        "validate_flow_roundtrip_output",
    }
    missing = sorted(name for name in required if name not in text and name not in functions)
    if missing:
        fail("runtime_transport", f"missing required interface/adapter names: {', '.join(missing)}")
    else:
        ok("runtime_transport", "sync/async protocols and serial/BLE adapters are present")
    if "Runtime app pages incomplete" not in text or "cached BLE peripheral for" not in text:
        fail("runtime_transport", "page-combine and BLE cache-name regression guards are not present")
    else:
        ok("runtime_transport", "app page and BLE cache-name guards are present")


def audit_app_store_bridge() -> None:
    rel = "scripts/app_store_server.py"
    text = read(rel)
    functions = python_functions(rel)
    if "run_transport_operation" not in functions:
        fail("app_store_bridge", "missing run_transport_operation")
    else:
        ok("app_store_bridge", "one RuntimeTransport runner is present")
    forbidden = {"run_ble_install", "run_serial_install"}
    forbidden_hits = sorted(name for name in functions if name in forbidden or name.startswith("read_ble_runtime_"))
    if forbidden_hits:
        fail("app_store_bridge", f"per-endpoint transport helpers remain: {', '.join(forbidden_hits)}")
    else:
        ok("app_store_bridge", "no per-endpoint BLE/serial install/status helpers remain")
    for endpoint in ("/api/runtime/capabilities", "/api/runtime/apps", "/api/transport/status"):
        if endpoint not in text:
            fail("app_store_bridge", f"missing HTTP endpoint {endpoint}")
    if not any(item[0] == "fail" and item[1].startswith("app_store_bridge") for item in CHECKS):
        ok("app_store_bridge", "runtime HTTP endpoints are exposed")


def audit_desktop_voice_bridge() -> None:
    require_contains("voice_bridge", "scripts/voice_bridge_serial.py", "from voice_bridge_common import")
    require_contains("voice_bridge", "scripts/voice_bridge_ble.py", "from voice_bridge_common import")
    require_contains("voice_bridge", "scripts/voice_bridge_serial.py", "SerialTransport(")
    require_contains("voice_bridge", "scripts/voice_bridge_ble.py", "BLETransport(")
    for rel in ("scripts/voice_bridge_serial.py", "scripts/voice_bridge_ble.py"):
        for forbidden in ("def write_wav", "def append_jsonl", "def run_reply_command"):
            if forbidden in read(rel):
                fail("voice_bridge", f"{rel} still defines duplicated helper {forbidden}")
    if not any(item[0] == "fail" and item[1].startswith("voice_bridge") for item in CHECKS):
        ok("voice_bridge", "serial/BLE voice bridges share terminal evidence helpers")


def audit_ios_transport() -> None:
    client_rel = "mobile/ios/VibeBoardBLE/Sources/VibeBoardBLE/VibeBoardBLEClient.swift"
    model_rel = "mobile/ios/VibeBoardBLE/Sources/VibeBoardBLE/VibeBoardDemoModel.swift"
    require_contains("ios_transport", client_rel, "public protocol VibeBoardRuntimeTransport")
    require_contains("ios_transport", client_rel, "extension VibeBoardBLEClient: VibeBoardRuntimeTransport")
    require_contains("ios_transport", model_rel, "private var client: any VibeBoardRuntimeTransport")
    require_absent("ios_transport", model_rel, r"CoreBluetooth|CBPeripheral|CBCentralManager")


def audit_default_networking() -> None:
    proj = read("project/proj.conf")
    forbidden_enabled = [
        "CONFIG_WIFI=y",
        "CONFIG_PKG_USING_WEBCLIENT=y",
        "CONFIG_LWIP=y",
        "CONFIG_CFG_PAN=y",
        "CONFIG_VB_RUNTIME_ENABLE_HTTP_APP_OTA=y",
        "CONFIG_VB_RUNTIME_ENABLE_BT_PAN=y",
    ]
    hits = [item for item in forbidden_enabled if item in proj]
    if hits:
        fail("default_networking", f"default project enables board networking: {', '.join(hits)}")
    else:
        ok("default_networking", "default project config does not enable WiFi/HTTP/PAN Runtime networking")
    main = read("src/gui_apps/VibeBoard_Runtime/main.c")
    if "VB_RUNTIME_ENABLE_HTTP_APP_OTA" not in main or "VB_RUNTIME_ENABLE_BT_PAN" not in main:
        fail("default_networking", "firmware experiment macros for PAN/HTTP are missing or unguarded")
    else:
        ok("default_networking", "PAN/HTTP code paths are guarded by explicit experiment macros")


def audit_capability_model() -> None:
    py = read("scripts/runtime_package.py")
    swift = read("mobile/ios/VibeBoardBLE/Sources/VibeBoardBLE/RuntimePackage.swift")
    for capability in ("wifi", "http", "network", "ntp", "board_ip", "native", "nes", "camera", "gamepad", "i2s"):
        if capability not in py:
            fail("capability_model", f"Python package validator missing forbidden capability {capability}")
        if capability not in swift:
            fail("capability_model", f"Swift package validator missing forbidden capability {capability}")
    if not any(item[0] == "fail" and item[1].startswith("capability_model") for item in CHECKS):
        ok("capability_model", "Python and Swift validators include forbidden ESP32/network capability names")


def audit_lua_audio_runtime() -> None:
    require_contains("lua_audio_runtime", "src/SConscript", "third_party/lua/SConscript")
    require_contains("lua_audio_runtime", "src/gui_apps/VibeBoard_Runtime/SConscript", "Glob('*.c')")
    require_file("lua_audio_runtime", "src/gui_apps/VibeBoard_Runtime/vb_runtime_lua.c")
    require_file("lua_audio_runtime", "src/gui_apps/VibeBoard_Runtime/vb_runtime_audio.c")
    for needle in (
        'VB_LUA_ENGINE_NAME "lua-5.5-full"',
        "VB_LUA_MEMORY_LIMIT",
        "VB_LUA_INSTRUCTION_LIMIT",
        "lua_sethook",
        "luaopen_utf8",
    ):
        require_contains("lua_audio_runtime", "src/gui_apps/VibeBoard_Runtime/vb_runtime_lua.c", needle)
    for needle in (
        "AUDIO_TYPE_LOCAL_MUSIC",
        "vb_runtime_audio_play_wav",
        "vb_runtime_audio_read_json",
        "vb_runtime_audio_set_volume",
        "static char g_vb_audio_msh_json[VB_AUDIO_JSON_MAX]",
        "char *json = g_vb_audio_msh_json",
    ):
        require_contains("lua_audio_runtime", "src/gui_apps/VibeBoard_Runtime/vb_runtime_audio.c", needle)
    require_contains("lua_audio_runtime", "src/gui_apps/VibeBoard_Runtime/main.c", "vibe_audio_play")
    require_contains("lua_audio_runtime", "src/gui_apps/VibeBoard_Runtime/main.c", "!vb_is_resource_package_path(src)")
    require_contains("lua_audio_runtime", "scripts/runtime_package.py", '"lua.full"')
    require_contains("lua_audio_runtime", "scripts/runtime_package.py", '"audio.playback"')


def audit_huangshan_ui_kit() -> None:
    for rel in (
        "src/huangshan_ui/hs_ui_theme.h",
        "src/huangshan_ui/hs_ui_theme.c",
        "src/huangshan_ui/hs_ui_components.h",
        "src/huangshan_ui/hs_ui_components.c",
    ):
        require_file("huangshan_ui_kit", rel)
    require_contains("huangshan_ui_kit", "src/SConscript", "huangshan_ui/SConscript")
    require_contains("huangshan_ui_kit", "src/gui_apps/VibeBoard_Runtime/vb_runtime_lua.c", '"vibe_ui_metric"')
    require_contains("huangshan_ui_kit", "src/gui_apps/VibeBoard_Runtime/main.c", "hs_ui_metric_create")
    require_contains("huangshan_ui_kit", "src/gui_apps/VibeBoard_Runtime/main.c", "VB_MAX_SCRIPT_UI_COMPONENTS 8")
    require_contains("huangshan_ui_kit", "scripts/runtime_app_plan_writer.py", 'UI_KIT_VERSION = "huangshan-ui/v1"')
    require_contains("huangshan_ui_kit", "src/gui_apps/VibeBoard_Runtime/main.c", "vb_pomodoro_start")
    require_file("huangshan_ui_kit", "scripts/runtime_apps/pomodoro/main.lua")
    require_contains("huangshan_ui_kit", "src/gui_apps/VibeBoard_Runtime/main.c", "vb_breakout_start")
    require_file("huangshan_ui_kit", "scripts/runtime_apps/breakout/main.lua")
    require_contains("huangshan_ui_kit", "src/gui_apps/VibeBoard_Runtime/main.c", "vb_thunder_start")
    require_file("huangshan_ui_kit", "scripts/runtime_apps/thunder_wing/main.lua")
    require_contains("huangshan_ui_kit", "src/gui_apps/VibeBoard_Runtime/main.c", "vb_imu_start")
    require_file("huangshan_ui_kit", "scripts/runtime_apps/imu_lab/main.lua")


def audit_voice_memory_lifecycle() -> None:
    main = read("src/gui_apps/VibeBoard_Runtime/main.c")
    transport = read("scripts/runtime_transport.py")
    for needle in (
        "vb_runtime_voice_required_bytes",
        "vb_runtime_voice_allocate_buffer(duration_ms)",
        "vb_runtime_voice_release_buffer",
        "app_cache_alloc(required_bytes, IMAGE_CACHE_PSRAM)",
        "app_cache_free(g_vb_voice.buffer)",
        "rt_ringbuffer_destroy(g_vb_voice.stream_rb)",
        "#define VB_VOICE_MAX_MS 3000",
    ):
        if needle not in main:
            fail("voice_memory", f"voice capture memory lifecycle missing {needle!r}")
    if "VB_VOICE_MAX_BYTES" in main:
        fail("voice_memory", "voice capture must not preallocate the maximum-duration buffer")
    if "finally:\n            self.voice_clear()" not in transport:
        fail("voice_memory", "serial voice capture does not release board PCM after transfer")
    if "finally:\n            await self.voice_clear()" not in transport:
        fail("voice_memory", "BLE voice capture does not release board PCM after transfer")
    if not any(item[0] == "fail" and item[1].startswith("voice_memory") for item in CHECKS):
        ok("voice_memory", "voice PCM uses bounded PSRAM/ring buffers and is released after transfer")


def audit_msh_stack_usage() -> None:
    main = read("src/gui_apps/VibeBoard_Runtime/main.c")
    msh = main.split("#ifdef FINSH_USING_MSH", 1)[-1]
    if "static char g_vb_msh_text[VB_APP_JSON_MAX]" not in main:
        fail("msh_stack", "shared static MSH response buffer is missing")
    for forbidden in (
        "char json[VB_APP_JSON_MAX]",
        "char json[VB_BLE_STATUS_MAX]",
        "char text[VB_BLE_STATUS_MAX]",
        "char json[VB_JSON_READ_MAX]",
    ):
        if forbidden in msh:
            fail("msh_stack", f"MSH command still allocates a large response buffer: {forbidden}")
    if "rt_malloc(VB_JSON_READ_MAX)" not in main or "rt_free(json)" not in main:
        fail("msh_stack", "long JSON source buffer is not heap-backed with explicit release")
    if "vb_runtime_audio_read_json(json, VB_JSON_READ_MAX)" not in main:
        fail("msh_stack", "json_read audio is not routed to the audio JSON provider")
    if not any(item[0] == "fail" and item[1].startswith("msh_stack") for item in CHECKS):
        ok("msh_stack", "large MSH responses avoid the 4 KiB shell stack")


def audit_high_risk_capability_evaluation() -> None:
    rel = "docs/runtime-high-risk-capabilities-evaluation.md"
    text = read(rel)
    for phrase in ("完整 Lua VM", "Native module ABI", "NES", "Camera", "Gamepad", "I2S", "暂缓"):
        if phrase not in text:
            fail("high_risk_capability_evaluation", f"{rel} missing {phrase!r}")
    for rel2 in ("docs/runtime-boundary.md", "docs/runtime-capabilities.md", "docs/runtime-app-plan-writer.md"):
        doc = read(rel2)
        if "runtime-high-risk-capabilities-evaluation.md" not in doc:
            fail("high_risk_capability_evaluation", f"{rel2} does not link high-risk evaluation")
    if not any(item[0] == "fail" and item[1].startswith("high_risk_capability_evaluation") for item in CHECKS):
        ok("high_risk_capability_evaluation", "high-risk Runtime capability evaluation and links are present")


def audit_voice_stop_contract() -> None:
    required = {
        "src/gui_apps/VibeBoard_Runtime/vb_runtime_lua.c": '"vibe_voice_stop"',
        "src/gui_apps/VibeBoard_Runtime/main.c": '"vibe_voice_stop"',
        "scripts/runtime_package.py": '"vibe_voice_stop"',
        "scripts/runtime_transport.py": "def voice_stop(",
        "mobile/ios/VibeBoardBLE/Sources/VibeBoardBLE/RuntimePackage.swift": '"vibe_voice_stop"',
    }
    for rel, token in required.items():
        if token not in read(rel):
            fail("voice_stop_contract", f"{rel} does not expose {token}")
    if not any(item[0] == "fail" and item[1].startswith("voice_stop_contract") for item in CHECKS):
        ok("voice_stop_contract", "voice_stop is aligned across Lua, firmware, Python and Swift")


def audit_codex_pet_bridge() -> None:
    protocol = read("scripts/codex_pet_protocol.py")
    bridge = read("scripts/codex_pet_bridge.py")
    helper = read("src/gui_apps/VibeBoard_Runtime/vb_runtime_codex_pet.c")
    runtime_main = read("src/gui_apps/VibeBoard_Runtime/main.c")
    for token in (
        "VB_PET_RECONNECT_AFTER_MS",
        '"Reconnecting %lus"',
        '"Approval required"',
        '\\"recentTasks\\"',
        "vb_pet_ticks_to_ms",
    ):
        if token not in helper:
            fail("codex_pet_bridge", f"Codex Pet task visibility is missing {token!r}")
    required_protocol = (
        'PROTOCOL_VERSION = "pet/v1"',
        "MAX_WIRE_BYTES = 192",
        "MAX_TTL_MS = 30_000",
        "class SequenceWindow:",
        "class EventReducer:",
    )
    required_bridge = (
        "class TransportCommandQueue:",
        "class DeviceSession:",
        "class TaskJournal:",
        "class LocalIPCServer:",
        'os.chmod(self.path, 0o600)',
        'os.chmod(temporary, 0o600)',
        '"inputSha256"',
    )
    for token in required_protocol:
        if token not in protocol:
            fail("codex_pet_bridge", f"pet/v1 protocol is missing {token!r}")
    for token in required_bridge:
        if token not in bridge:
            fail("codex_pet_bridge", f"single-owner Bridge is missing {token!r}")
    if bridge.count("BLETransport(options)") != 1:
        fail("codex_pet_bridge", "codex_pet_bridge.py must instantiate exactly one BLETransport")
    if "promptSha256" in bridge:
        fail("codex_pet_bridge", "task journal must not use the obsolete promptSha256 field")
    if "int(time.time() * 1000) & 0xFFFFFFFF" not in bridge:
        fail("codex_pet_bridge", "Bridge sequence seed must advance across rapid restarts")
    if "vb_pet_sequence_newer" not in helper:
        fail("codex_pet_bridge", "board sequence checks must tolerate uint32 wraparound")
    receive_start = helper.find("void vb_codex_pet_receive_flow(")
    tick_start = helper.find("void vb_codex_pet_tick(", receive_start)
    receive_body = helper[receive_start:tick_start] if receive_start >= 0 and tick_start > receive_start else ""
    if "vb_pet_enqueue_flow(channel, sequence, payload);" not in receive_body:
        fail("codex_pet_bridge", "BLE flow ingress must enqueue work for the LVGL thread")
    if "g_pet." in receive_body:
        fail("codex_pet_bridge", "BLE flow ingress must not mutate LVGL-owned pet state")
    status_start = helper.find("static const char *vb_pet_status_text(void)")
    state_name_start = helper.find("static const char *vb_pet_state_name", status_start)
    status_body = helper[status_start:state_name_start] if status_start >= 0 and state_name_start > status_start else ""
    if 'strstr(g_pet.task_detail, "approval")' in status_body or \
       'strstr(g_pet.task_detail, "Approval")' in status_body:
        fail("codex_pet_bridge", "approval status must not be inferred from task detail text")
    for token in (
        "vb_pet_detail_is_approval",
        "!approval && vb_pet_detail_is_approval(g_pet.task_detail)",
        "lv_obj_add_flag(g_pet.transcript_label, LV_OBJ_FLAG_HIDDEN)",
        "VB_PET_TASK_LABEL_COMPACT_Y",
    ):
        if token not in helper:
            fail("codex_pet_bridge", f"approval duplicate suppression is missing {token!r}")
    for token in ("vb_pet_drain_flows", "preloaded_data", "vb_pet_publish_status"):
        if token not in helper:
            fail("codex_pet_bridge", f"board pet concurrency guard is missing {token!r}")
    project_config = read("project/proj.conf")
    if "CONFIG_IMAGE_CACHE_IN_PSRAM_SIZE=2100000" not in project_config:
        fail("codex_pet_bridge", "compressed Petdex preload requires the 2.1 MB PSRAM image pool")
    if "CONFIG_PKG_USING_ZLIB=y" not in project_config:
        fail("codex_pet_bridge", "compressed Petdex preload requires zlib")
    if "#define VB_TF_SPI_MAX_HZ (6u * 1000u * 1000u)" not in read("src/app_utils/main.c"):
        fail("codex_pet_bridge", "TF SPI must remain at the runtime-load-safe 6 MHz ceiling")
    storage = read("src/gui_apps/VibeBoard_Runtime/vb_runtime_storage.c")
    storage_header = read("src/gui_apps/VibeBoard_Runtime/vb_runtime_storage.h")
    audio = read("src/gui_apps/VibeBoard_Runtime/vb_runtime_audio.c")
    for source, label in ((helper, "pet loader"), (audio, "audio worker")):
        if "vb_runtime_storage_take(" not in source or "vb_runtime_storage_release();" not in source:
            fail("codex_pet_bridge", f"{label} is outside the shared Runtime storage transaction")
    for token in ("rt_mutex_init", "rt_mutex_take", "rt_mutex_release"):
        if token not in storage:
            fail("codex_pet_bridge", f"Runtime storage transaction is missing {token!r}")
    if "#define VB_RUNTIME_STORAGE_IO_CHUNK_BYTES 512u" not in storage_header:
        fail("codex_pet_bridge", "Runtime SD reads must avoid the SPI-MSD multi-block path")
    if "#define VB_AUDIO_IO_CHUNK VB_RUNTIME_STORAGE_IO_CHUNK_BYTES" not in audio:
        fail("codex_pet_bridge", "WAV reads must use single-sector SD transactions")
    for token in (
        "vb_runtime_audio_preload_codex_cues",
        "vb_runtime_audio_release_codex_cues",
        "vb_audio_find_cached_wav",
        "vb_audio_parse_wav_memory",
        "VB_AUDIO_CODEX_CUE_COUNT 5",
        '\\"cachedCues\\"',
    ):
        if token not in audio:
            fail("codex_pet_bridge", f"Codex cues must remain runtime-SD-free: {token!r}")
    if "vb_runtime_audio_preload_codex_cues();" not in runtime_main:
        fail("codex_pet_bridge", "Codex cue cache must be populated before the pet UI starts")
    if "VB_PET_PRELOAD_IO_CHUNK_BYTES (8u * 1024u)" not in helper:
        fail("codex_pet_bridge", "startup-only pet preload must use bounded 8 KiB SD reads")
    read_start = helper.find("static int vb_pet_read_full(")
    read_end = helper.find("static void vb_pet_release_rocky_frames", read_start)
    read_body = helper[read_start:read_end] if read_start >= 0 and read_end > read_start else ""
    if "rt_thread_mdelay" in read_body:
        fail("codex_pet_bridge", "single-sector preload reads must not sleep for one RTOS tick per sector")
    for token in (
        "vb_pet_preload_assets",
        "vb_pet_validate_preload_index",
        "vb_pet_fill_preloaded_assets_unlocked",
        "vb_pet_activate_preloaded_state",
        "VB_PET_PRELOAD_MAGIC 0x43504256u",
        "VB_PET_MAX_ASSETS 1",
        "VB_PET_PRELOAD_PACK_STATE_COUNT VB_PET_ASSET_STATE_COUNT",
        "VB_PET_PRELOAD_STATE_COUNT VB_PET_ASSET_STATE_COUNT",
        "VB_PET_PRELOAD_MAX_BYTES (900000u)",
        "VB_PET_PRELOAD_IO_CHUNK_BYTES (8u * 1024u)",
        "VB_PET_RUNTIME_FRAME_LIMIT 2",
        "VB_PET_NATIVE_FRAME_MS 180",
        "preloaded_frames",
        "uncompress(cursor",
        "lv_img_set_zoom(g_pet.pet_image",
        "rt_thread_yield();",
        '\\"preloadedBytes\\"',
        "g_pet.custom_displayed_frame = -1;",
        "custom_displayed_frame == g_pet.custom_frame_index",
    ):
        if token not in helper:
            fail("codex_pet_bridge", f"pet assets must remain startup-preloaded: {token!r}")
    if "VB_PET_IMAGE_RUNNING_ZOOM" in helper or "VB_PET_IMAGE_RUNNING_RAISED_Y" in helper:
        fail("codex_pet_bridge", "custom pet animation must not animate zoom or position")
    if "VB_PET_NATIVE_FRAME_MS" not in helper or "g_pet.custom_frame_ms = VB_PET_NATIVE_FRAME_MS;" not in helper:
        fail("codex_pet_bridge", "custom pet states must use native frame timing")
    for forbidden in ("vb_pet_custom_loader", 'rt_thread_create("vbpetload"'):
        if forbidden in helper:
            fail("codex_pet_bridge", f"runtime pet SD loading must stay removed: {forbidden!r}")
    if "PET_READY_TIMEOUT_SECONDS = 30.0" not in read("scripts/codex_pet_bridge.py"):
        fail("codex_pet_bridge", "cold-boot ready gate must cover the bounded startup preload")
    keychain_launcher = read("scripts/codex_pet_test_backend.command")
    monitor = read("scripts/codex_pet_monitor.py")
    monitor_launcher = read("scripts/codex_pet_monitor.command")
    approval_helper = read("scripts/codex_pet_desktop_approval.swift")
    for token in (
        "class DesktopTaskRegistry:",
        "class CodexDesktopMonitor:",
        'TASKS_CHANNEL = "pet.tasks"',
        "APPROVAL_TTL_MS",
        "SubprocessApprovalExecutor",
    ):
        if token not in monitor:
            fail("codex_pet_bridge", f"desktop monitor is missing {token!r}")
    for token in ("--mode monitor", "CodexPetDesktopApproval", "swiftc"):
        if token not in monitor_launcher:
            fail("codex_pet_bridge", f"desktop monitor launcher is missing {token!r}")
    for token in ("AXIsProcessTrusted", "codex://threads/", "safePair", "kAXPressAction"):
        if token not in approval_helper:
            fail("codex_pet_bridge", f"restricted desktop approval helper is missing {token!r}")
    for token in (
        "find-generic-password",
        "add-generic-password",
        "delete-generic-password",
        "KEYCHAIN_SERVICE=",
    ):
        if token not in keychain_launcher:
            fail("codex_pet_bridge", f"Keychain test launcher is missing {token!r}")
    if "ZHIPU_API_KEY=" in keychain_launcher and "ZHIPU_API_KEY=\"$(read_api_key)\"" not in keychain_launcher:
        fail("codex_pet_bridge", "test launcher must not contain a literal GLM API key")
    if re.search(r"^\s*status=", keychain_launcher, re.MULTILINE):
        fail("codex_pet_bridge", "zsh test launcher must not assign the read-only status parameter")
    if not any(item[0] == "fail" and item[1].startswith("codex_pet_bridge") for item in CHECKS):
        ok("codex_pet_bridge", "pet/v1 and the single-owner Bridge contracts are present")


def audit_codex_pet_voice_app() -> None:
    required = (
        ("src/gui_apps/VibeBoard_Runtime/vb_runtime_codex_pet.h", "vb_codex_pet_receive_flow"),
        ("src/gui_apps/VibeBoard_Runtime/vb_runtime_codex_pet.c", "static void vb_pet_begin_voice(void)"),
        ("src/gui_apps/VibeBoard_Runtime/main.c", '#include "vb_runtime_codex_pet.h"'),
        ("src/gui_apps/VibeBoard_Runtime/main.c", '"vibe_codex_pet"'),
        ("src/gui_apps/VibeBoard_Runtime/main.c", 'VIBEBOARD_CODEX_PET_APP_ID "codex_pet"'),
        ("src/gui_apps/VibeBoard_Runtime/main.c", "rt_strcmp(active, VIBEBOARD_CODEX_PET_APP_ID)"),
        ("src/gui_apps/VibeBoard_Runtime/main.c", "static void vb_runtime_clear_root_children(void)"),
        ("src/gui_apps/VibeBoard_Runtime/main.c", "vb_runtime_clear_root_children();"),
        ("src/gui_apps/VibeBoard_Runtime/main.c", "volatile int reload_in_progress;"),
        ("src/gui_apps/VibeBoard_Runtime/main.c", '"[vb_runtime] app launch already pending/running: %s\\n"'),
        ("src/gui_apps/VibeBoard_Runtime/main.c", '"[vb_runtime] app transition already pending: %s\\n"'),
        ("src/gui_apps/VibeBoard_Runtime/main.c", "g_vb_runtime.pending_reload || g_vb_runtime.reload_in_progress"),
        ("src/gui_apps/VibeBoard_Runtime/main.c", "vb_codex_pet_rgb_set"),
        ("src/gui_apps/VibeBoard_Runtime/vb_runtime_lua.c", '"vibe_codex_pet"'),
        ("scripts/runtime_package.py", '"vibe_codex_pet"'),
        ("mobile/ios/VibeBoardBLE/Sources/VibeBoardBLE/RuntimePackage.swift", '"vibe_codex_pet"'),
        ("scripts/runtime_apps/codex_pet/main.lua", "vibe_codex_pet"),
        ("scripts/runtime_apps/codex_pet/manifest.json", '"id": "codex_pet"'),
        ("scripts/codex_pet_voice.py", "class CodexPetVoiceService:"),
        ("scripts/voice_bridge_common.py", "class VoiceStreamCollector:"),
        ("scripts/pager_voice_bridge.py", "from voice_bridge_common import ("),
        ("scripts/codex_pet_status.py", "class CodexPetStatusService:"),
        ("scripts/codex_pet_status.py", "async def observe_external("),
        ("scripts/codex_pet_status.py", 'QUOTA_CHANNEL = "pet.quota"'),
        ("scripts/codex_pet_hook.py", '"PermissionRequest": ("needs_input", "approval", "Approval required")'),
        ("scripts/codex_pet_hook.py", 'str(Path(tempfile.gettempdir()) / f"huangshan-codex-pet-{os.getuid()}.sock")'),
        ("scripts/codex_pet_hook.py", "def ack_accepted("),
    )
    for rel, token in required:
        if token not in read(rel):
            fail("codex_pet_voice_app", f"{rel} does not contain {token!r}")
    main = read("src/gui_apps/VibeBoard_Runtime/main.c")
    helper = read("src/gui_apps/VibeBoard_Runtime/vb_runtime_codex_pet.c")
    start_body = main[main.find("static void on_start(void)"):main.find("static void on_stop(void)")]
    for token in ("g_vb_runtime.reload_in_progress = 1;", "vb_load_active_package();",
                  "g_vb_runtime.reload_in_progress = 0;"):
        if token not in start_body:
            fail("codex_pet_voice_app", f"Runtime startup transition guard is missing {token!r}")
    reload_start = main.rfind("static void vb_runtime_request_reload(void)")
    reload_body = main[
        reload_start:
        main.find("static int vb_runtime_select_app", reload_start)
    ]
    if "g_vb_runtime.pending_reload = 1;" not in reload_body:
        fail("codex_pet_voice_app", "Runtime reload no longer publishes a GUI-thread request")
    if re.search(r"\blv_timer_ready\s*[(]", reload_body):
        fail("codex_pet_voice_app", "Runtime reload touches an LVGL timer from BLE/shell worker threads")
    for token in ("vb_runtime_install_blob", "VB_RUNTIME_INSTALL_BLOB_CHUNK_BYTES 4096u",
                  "encoding=base64"):
        if token not in main:
            fail("codex_pet_voice_app", f"binary serial installer is missing {token!r}")
    serial_transport = read("scripts/runtime_transport.py")
    if "def install_package_binary(" not in serial_transport or "base64.b64encode" not in serial_transport:
        fail("codex_pet_voice_app", "desktop binary serial installer is missing")
    provisioning = read("scripts/provision_codex_pet_board.sh")
    for token in ("--binary-install", "--codex-pet-only", "--final-wait 3", "--ready-timeout 90"):
        if token not in provisioning:
            fail("codex_pet_voice_app", f"new-board provisioning is missing {token!r}")
    rocky_extractor = read("scripts/extract_codex_rocky.js")
    if "static void vb_pet_begin_voice(void)" in main:
        fail("codex_pet_voice_app", "Codex Pet business state leaked into Runtime main.c")
    for token in ("pet.new", "pet.continue", "pet.transcript", "pet.task.ack", "pet.asr.error"):
        if token not in helper and token not in read("scripts/codex_pet_voice.py"):
            fail("codex_pet_voice_app", f"Codex Pet flow is missing {token!r}")
    for token in ("pet.quota", "vb_pet_rgb_tick", "rgb_set"):
        if token not in helper and token not in main:
            fail("codex_pet_voice_app", f"Codex Pet status/RGB flow is missing {token!r}")
    for token in ('rt_strcmp(channel, "pet.tasks")',
                  'lv_label_set_text(g_pet.new_label, "Allow")',
                  'lv_label_set_text(g_pet.continue_label, "Deny")',
                  "vb_pet_handle_horizontal_swipe", "LV_OBJ_FLAG_GESTURE_BUBBLE",
                  "VB_PET_VOICE_UI_ENABLED 0", "g_pet.task_state", "VB_PET_HEARTBEAT_TTL_MS"):
        if token not in helper:
            fail("codex_pet_voice_app", f"monitor-first board UI is missing {token!r}")
    for token in (
        "rocky-spritesheet-v5-",
        "integrity?.hash",
        "kernel: \"nearest\"",
        "EXPECTED_ROWS = 11",
        "ROCKY_RLE_MAGIC",
        "encodeRleFrame",
    ):
        if token not in rocky_extractor:
            fail("codex_pet_voice_app", f"verified Rocky extractor is missing {token!r}")
    for token in (
        "VB_PET_ROCKY_DIR",
        "VB_PET_ROCKY_RLE_MAGIC",
        "VB_PET_RLE_RECORD_BATCH 800u",
        "g_vb_pet_rle_records",
        "g_vb_pet_rocky_paths",
        "vb_pet_load_rocky_frame",
        "app_cache_img_alloc",
        "IMAGE_CACHE_PSRAM",
        "vb_pet_release_rocky_frames",
        "vb_pet_update_rocky",
        "rocky_available",
    ):
        if token not in helper:
            fail("codex_pet_voice_app", f"Rocky board renderer is missing {token!r}")
    pet_importer = read("scripts/import_petdex_pets.js")
    pet_ignore = read("scripts/runtime_apps/codex_pet/.runtimeignore")
    for token in ("assets/pets/*/*", "assets/rocky/*"):
        if token not in pet_ignore:
            fail("codex_pet_voice_app", f"Codex Pet package must exclude desktop-only resource {token!r}")
    for token in (
        "STATE_PACK_VERSION = 2",
        "encodeRawFrame",
        "upgradeLegacyPet",
        "PRELOAD_PACK_MAGIC",
        "PRELOAD_STATES = STATE_ROWS.map",
        "activeSlug",
        "writeActiveCatalog",
        "buildPreloadPack",
        "verifyPreloadPack",
        "zlib.deflateSync",
    ):
        if token not in pet_importer:
            fail("codex_pet_voice_app", f"raw pet asset pipeline is missing {token!r}")
    if 'rt_strcmp(dot, ".rle")' not in main:
        fail("codex_pet_voice_app", "firmware package validator does not allow Rocky RLE resources")
    for token in (
        "BLE_GATT_PERM_WRITE_PERMISSION_UNAUTH",
        "BLE_GATT_PERM_NOTIFY_PERMISSION_UNAUTH",
        "connection_manager_get_enc_state",
        "connection_manager_set_bond_cnf_sec",
        "connection_manager_set_link_security",
        "security_requested[VB_BLE_TRACKED_CONNECTIONS]",
        "vb_ble_request_link_security",
        "already_pending",
        '(ind->conn_idx, "raw")',
        '(ind->conn_idx, "manager")',
        "g_vb_ble.security_requested[ind->conn_idx] = 0",
        "secure=%d",
    ):
        if token not in main:
            fail("codex_pet_voice_app", f"encrypted BLE gate is missing {token!r}")
    if main.count("connection_manager_set_link_security(") != 1:
        fail("codex_pet_voice_app", "BLE security must have one guarded request helper")
    transport = read("scripts/runtime_transport.py")
    for token in (
        "def validate_ble_runtime_status(status: str) -> None:",
        'values.get("api") != BLE_RUNTIME_STATUS_API',
        'values.get("secure") != "1"',
    ):
        if token not in transport:
            fail("codex_pet_voice_app", f"desktop BLE identity validation is missing {token!r}")
    for token in ("BLE_AUTHENTICATION_TIMEOUT_SECONDS", "_ble_authentication_pending"):
        if token not in read("scripts/runtime_transport.py"):
            fail("codex_pet_voice_app", f"desktop BLE pairing recovery is missing {token!r}")
    if "context=%s" not in main or r'\"context\":\"%s\"' not in main:
        fail("codex_pet_voice_app", "voice status does not expose the Codex Pet capture context")
    if not any(item[0] == "fail" and item[1].startswith("codex_pet_voice_app") for item in CHECKS):
        ok("codex_pet_voice_app", "native pet UI and the shared voice-to-Codex pipeline are present")


def audit_codex_pet_mcp() -> None:
    mcp_rel = "scripts/codex_pet_mcp.py"
    bridge_rel = "scripts/codex_pet_bridge.py"
    status_rel = "scripts/codex_pet_status.py"
    helper_rel = "src/gui_apps/VibeBoard_Runtime/vb_runtime_codex_pet.c"
    for rel in (mcp_rel, "docs/codex-pet-mcp.toml"):
        require_file("codex_pet_mcp", rel)
    required = (
        (mcp_rel, 'PROTOCOL_VERSION = "2025-06-18"'),
        (mcp_rel, '"method": "tools/call"'),
        (mcp_rel, '"readOnlyHint"'),
        (mcp_rel, '"additionalProperties": False'),
        (bridge_rel, "class HardwareAuditLog:"),
        (bridge_rel, 'action == "hardware_command"'),
        (bridge_rel, 'source = "hook" if action == "hook_event" else "mcp"'),
        (status_rel, "async def resolve_approval("),
        (status_rel, '_BOARD_APPROVAL_METHODS'),
        (helper_rel, 'rt_strcmp(channel, "pet.approval")'),
        (helper_rel, 'g_pet.ops.send_action("approve"'),
        (helper_rel, 'g_pet.ops.send_action("deny"'),
        ("src/gui_apps/VibeBoard_Runtime/main.c", 'vb_ble_notify_status();'),
        ("scripts/runtime_transport.py", "next_status_notification"),
    )
    for rel, token in required:
        if token not in read(rel):
            fail("codex_pet_mcp", f"{rel} does not contain {token!r}")
    mcp = read(mcp_rel)
    tool_names = set(re.findall(r'"name": "([a-z0-9_]+)"', mcp))
    forbidden = {"raw_gpio", "read_file", "write_file", "flash_firmware", "capture_microphone"}
    exposed = sorted(tool_names & forbidden)
    if exposed:
        fail("codex_pet_mcp", "dangerous MCP tools are exposed: " + ", ".join(exposed))
    if "BLETransport(" in mcp or "BleakClient" in mcp:
        fail("codex_pet_mcp", "MCP server bypasses the single-owner Bridge")
    if not any(item[0] == "fail" and item[1].startswith("codex_pet_mcp") for item in CHECKS):
        ok("codex_pet_mcp", "MCP tools, audit boundary, and one-shot physical approvals are present")


def audit_codex_pet_audio() -> None:
    required_files = (
        "scripts/codex_pet_audio.py",
        "scripts/generate_codex_pet_cues.py",
        "docs/codex-pet-audio-evaluation.md",
        "scripts/runtime_apps/codex_pet/assets/listening.wav",
        "scripts/runtime_apps/codex_pet/assets/submitted.wav",
        "scripts/runtime_apps/codex_pet/assets/needs_input.wav",
        "scripts/runtime_apps/codex_pet/assets/done.wav",
        "scripts/runtime_apps/codex_pet/assets/error.wav",
    )
    for rel in required_files:
        require_file("codex_pet_audio", rel)
    required = (
        ("src/gui_apps/VibeBoard_Runtime/vb_runtime_audio.c", "vb_runtime_audio_prepare_capture"),
        ("src/gui_apps/VibeBoard_Runtime/vb_runtime_audio.c", "g_vb_audio.capture_reserved"),
        ("src/gui_apps/VibeBoard_Runtime/vb_runtime_audio.c", "result == -RT_EINTR ? RT_EOK : result"),
        ("src/gui_apps/VibeBoard_Runtime/main.c", "vb_runtime_audio_finish_capture();"),
        ("src/gui_apps/VibeBoard_Runtime/main.c", "vb_codex_pet_cue_play"),
        ("src/gui_apps/VibeBoard_Runtime/main.c", "State changes must never block the LVGL thread"),
        ("src/gui_apps/VibeBoard_Runtime/vb_runtime_audio.c", "RT_THREAD_PRIORITY_MIDDLE + 8"),
        ("src/gui_apps/VibeBoard_Runtime/vb_runtime_codex_pet.c", 'g_pet.ops.cue_play("listening")'),
        ("src/gui_apps/VibeBoard_Runtime/vb_runtime_codex_pet.c", "vb_pet_local_voice_active"),
        ("src/gui_apps/VibeBoard_Runtime/vb_runtime_codex_pet.c", "vb_pet_reset_voice_capture"),
        ("scripts/codex_pet_mcp.py", '"name": "huangshan_play_cue"'),
        ("scripts/codex_pet_mcp.py", '"name": "huangshan_stop_audio"'),
        ("scripts/codex_pet_audio.py", "TTS_MAX_WAV_BYTES = 512 * 1024"),
        ("scripts/codex_pet_audio.py", "def wake_word_gate("),
    )
    for rel, token in required:
        if token not in read(rel):
            fail("codex_pet_audio", f"{rel} does not contain {token!r}")
    if "g_vb_audio.last_error = 1;" in read("src/gui_apps/VibeBoard_Runtime/vb_runtime_audio.c"):
        fail("codex_pet_audio", "audio in-progress state is exposed as a false error")
    if not any(item[0] == "fail" and item[1].startswith("codex_pet_audio") for item in CHECKS):
        ok("codex_pet_audio", "optional cues, capture arbitration, TTS policy, and KWS gates are present")


def audit_direct_transport_usage() -> None:
    allowed_direct_serial = {
        "scripts/runtime_transport.py",
        "scripts/flash.py",
        "scripts/monitor.sh",
        "scripts/monitor.ps1",
        "scripts/peer_reliability.py",
    }
    allowed_direct_ble = {
        "scripts/runtime_transport.py",
    }
    direct_serial: list[str] = []
    direct_ble: list[str] = []
    for path in sorted((ROOT / "scripts").glob("*.py")):
        rel = path.relative_to(ROOT).as_posix()
        if rel == "scripts/runtime_architecture_audit.py":
            continue
        text = path.read_text(encoding="utf-8")
        if re.search(r"(^|[^A-Za-z_])serial[.]Serial[(]|^import serial|from serial", text, re.MULTILINE):
            if rel not in allowed_direct_serial:
                direct_serial.append(rel)
        if "BleakClient" in text or "BleakScanner" in text or "from bleak" in text:
            if rel not in allowed_direct_ble:
                direct_ble.append(rel)
    if direct_serial:
        fail("direct_transport_usage", "unexpected direct pyserial users: " + ", ".join(direct_serial))
    else:
        ok("direct_transport_usage", "direct pyserial use is limited to RuntimeTransport and hardware diagnostics")
    if direct_ble:
        fail("direct_transport_usage", "unexpected direct Bleak users: " + ", ".join(direct_ble))
    else:
        ok("direct_transport_usage", "direct Bleak use is limited to RuntimeTransport")


def run_audit() -> int:
    CHECKS.clear()
    audit_runtime_transport()
    audit_app_store_bridge()
    audit_desktop_voice_bridge()
    audit_ios_transport()
    audit_default_networking()
    audit_capability_model()
    audit_lua_audio_runtime()
    audit_huangshan_ui_kit()
    audit_voice_memory_lifecycle()
    audit_msh_stack_usage()
    audit_high_risk_capability_evaluation()
    audit_voice_stop_contract()
    audit_codex_pet_bridge()
    audit_codex_pet_voice_app()
    audit_codex_pet_mcp()
    audit_codex_pet_audio()
    audit_direct_transport_usage()
    failures = [message for status, message in CHECKS if status == "fail"]
    for status, message in CHECKS:
        print(f"[{status}] {message}")
    if failures:
        print(f"runtime architecture audit failed: {len(failures)} issue(s)", file=sys.stderr)
        return 1
    print("runtime architecture audit ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit Huangshan RuntimeTransport architecture invariants.")
    parser.add_argument("--self-test", action="store_true", help="Run the architecture audit.")
    args = parser.parse_args()
    if args.self_test:
        return run_audit()
    parser.error("use --self-test")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
