#!/usr/bin/env python3
from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import json
import re
import sys
import tempfile
import threading
from pathlib import Path


_ERROR_MODE = threading.local()


class RuntimePackageError(ValueError):
    pass


@contextlib.contextmanager
def package_errors_as_exceptions():
    previous = getattr(_ERROR_MODE, "raise_errors", False)
    _ERROR_MODE.raise_errors = True
    try:
        yield
    finally:
        _ERROR_MODE.raise_errors = previous


SAFE_APP_ID = re.compile(r"^[a-z][a-z0-9_]{0,14}$")
INSTALL_COMMAND_MAX_CHARS = 250
SAFE_PATH = re.compile(
    r"^(manifest\.json|app\.info|main\.lua|files\.txt|README\.md|"
    r"(?:assets|images|fonts|lib)/[A-Za-z0-9_./-]+\."
    r"(?:json|txt|png|jpg|jpeg|bin|rle|ttf|otf|lua|wav))$"
)

LUA_NAME = r"[A-Za-z_][A-Za-z0-9_]*"
LUA_CALL = re.compile(rf"^(?:local\s+{LUA_NAME}\s*=\s*)?{LUA_NAME}\s*\(.*\)$")
LUA_UNSUPPORTED_PREFIXES = (
    "function",
    "for",
    "while",
    "repeat",
    "if",
    "elseif",
    "else",
    "end",
    "return",
    "require",
    "dofile",
    "load",
    "loadfile",
    "pcall",
    "xpcall",
)
MANIFEST_CAPABILITIES = {
    "status",
    "clock",
    "reload",
    "vibeboard.launcher.reload",
    "game",
    "weather.current",
    "display.brightness",
    "display.size",
    "display.resolution",
    "display.state",
    "display.bpp",
    "screen.brightness",
    "screen.size",
    "vibeboard.display.brightness",
    "vibeboard.display.size",
    "vibeboard.display.state",
    "vibeboard.display.bpp",
    "battery",
    "charger",
    "power.battery",
    "power.charger",
    "power.charger.status",
    "power.charger.state",
    "power.charger.det",
    "power.charger.en",
    "power.charger.fault",
    "vibeboard.power.battery",
    "vibeboard.power.charger",
    "vibeboard.power.charger.status",
    "vibeboard.power.charger.state",
    "vibeboard.power.charger.det",
    "vibeboard.power.charger.en",
    "vibeboard.power.charger.fault",
    "flow.latest",
    "flow.summary",
    "flow.payload",
    "flow.text",
    "flow.channel",
    "flow.seq",
    "flow.sequence",
    "flow.bytes",
    "flow.total",
    "flow.retained",
    "flow.count",
    "flow.capacity",
    "vibeboard.flow.latest",
    "vibeboard.flow.summary",
    "vibeboard.flow.payload",
    "vibeboard.flow.text",
    "vibeboard.flow.channel",
    "vibeboard.flow.seq",
    "vibeboard.flow.sequence",
    "vibeboard.flow.bytes",
    "vibeboard.flow.total",
    "vibeboard.flow.retained",
    "vibeboard.flow.count",
    "vibeboard.flow.capacity",
    "peer.status",
    "peer.messages",
    "peer.send",
    "peer.pair",
    "peer.state",
    "peer.latest",
    "peer.unread",
    "peer.pending",
    "vibeboard.peer.status",
    "vibeboard.peer.messages",
    "vibeboard.peer.send",
    "vibeboard.peer.pair",
    "voice.ready",
    "voice.recording",
    "voice.state",
    "voice.seq",
    "voice.bytes",
    "voice.duration",
    "voice.dropped",
    "voice.error",
    "voice.rate",
    "voice.built",
    "voice.available",
    "voice.start",
    "voice.record",
    "voice.stop",
    "voice.clear",
    "vibeboard.voice.ready",
    "vibeboard.voice.recording",
    "vibeboard.voice.state",
    "vibeboard.voice.seq",
    "vibeboard.voice.bytes",
    "vibeboard.voice.duration",
    "vibeboard.voice.dropped",
    "vibeboard.voice.error",
    "vibeboard.voice.rate",
    "vibeboard.voice.built",
    "vibeboard.voice.available",
    "vibeboard.voice.start",
    "vibeboard.voice.record",
    "vibeboard.voice.stop",
    "vibeboard.voice.clear",
    "sensor.light",
    "sensor.mag",
    "sensor.acce",
    "sensor.accel",
    "sensor.gyro",
    "sensor.step",
    "vibeboard.sensor.light",
    "vibeboard.sensor.mag",
    "vibeboard.sensor.acce",
    "vibeboard.sensor.gyro",
    "vibeboard.sensor.step",
    "touch.last",
    "touch.count",
    "touch.event",
    "touch.gesture",
    "touch.delta",
    "touch.duration",
    "touch.active",
    "vibeboard.touch.last",
    "vibeboard.touch.count",
    "vibeboard.touch.event",
    "vibeboard.touch.gesture",
    "vibeboard.touch.delta",
    "vibeboard.touch.duration",
    "vibeboard.touch.active",
    "gpio.key1",
    "gpio.key1.level",
    "gpio.key2",
    "gpio.key2.level",
    "vibeboard.gpio.key1",
    "vibeboard.gpio.key1.level",
    "vibeboard.gpio.key2",
    "vibeboard.gpio.key2.level",
    "audio.state",
    "audio.playback",
    "audio.stop",
    "audio.progress",
    "audio.format",
    "audio.volume",
    "audio.available",
    "audio.playing",
    "vibeboard.audio.state",
    "vibeboard.audio.playback",
    "vibeboard.audio.stop",
    "vibeboard.audio.progress",
    "vibeboard.audio.format",
    "vibeboard.audio.volume",
    "vibeboard.audio.available",
    "vibeboard.audio.playing",
}
MANIFEST_DECLARED_CAPABILITIES = MANIFEST_CAPABILITIES | {
    "assets",
    "audio",
    "ble",
    "bridge",
    "display",
    "flow",
    "fs",
    "gpio",
    "huangshan",
    "launcher",
    "lua-subset",
    "lua",
    "lua.full",
    "manifest",
    "power",
    "peer",
    "rgb",
    "screen",
    "sensor",
    "sensors",
    "serial",
    "storage",
    "touch",
    "voice",
    "weather",
}
HUANGSHAN_PROFILE_ALIASES = {
    "huangshan",
    "huangshan-pi",
    "vibeboard-huangshan",
    "sf32",
    "sf32lb52",
    "sf32lb525",
    "sf32lb525uc6",
}
MANIFEST_PROFILE_FIELDS = ("runtimeProfile", "runtime_profile", "targetProfile", "target")
MANIFEST_CAPABILITY_LIST_FIELDS = ("capabilities", "requires", "permissions")
MANIFEST_METADATA_STRING_FIELDS = {
    "category": 32,
    "icon": 32,
    "author": 64,
    "screenshot": 96,
}
MANIFEST_REQUIREMENTS_MAX = 8
MANIFEST_REQUIREMENT_MAX_LEN = 48
ESP32_NATIVE_CAPABILITY_NAMES = {
    "board_ip",
    "bluetooth.pan",
    "camera",
    "gamepad",
    "http",
    "i2s",
    "native",
    "nes",
    "network",
    "ntp",
    "pan",
    "wifi",
}
ESP32_NATIVE_CAPABILITY_PREFIXES = tuple(f"{name}." for name in ESP32_NATIVE_CAPABILITY_NAMES)
MANIFEST_COMPONENT_TYPES = {"status", "clock", "action", "label"}
HEX_COLOR = re.compile(r"^#[0-9a-fA-F]{6}$")


LUA_SUPPORTED_CALLS = {
    "lv_scr_act",
    "lv_obj_clean",
    "lv_obj_create",
    "lv_label_create",
    "lv_btn_create",
    "lv_img_create",
    "lv_obj_set_size",
    "lv_obj_set_width",
    "lv_obj_set_height",
    "lv_obj_set_pos",
    "lv_obj_align",
    "lv_obj_center",
    "lv_obj_set_style_bg_color",
    "lv_obj_set_style_text_color",
    "lv_obj_set_style_radius",
    "lv_obj_set_style_border_width",
    "lv_obj_set_style_border_color",
    "lv_obj_clear_flag",
    "lv_label_set_text",
    "lv_label_set_long_mode",
    "lv_img_set_src",
    "print",
    "vibe_label",
    "vibe_button",
    "vibe_image",
    "vibe_ui_header",
    "vibe_ui_metric",
    "vibe_ui_badge",
    "vibe_ui_progress",
    "vibe_ui_button",
    "vibe_read_file",
    "vibe_timer_label",
    "vibe_sensor_label",
    "vibe_touch_label",
    "vibe_gpio_label",
    "vibe_power_label",
    "vibe_display_label",
    "vibe_display_brightness",
    "vibe_voice_start",
    "vibe_voice_stop",
    "vibe_voice_clear",
    "vibe_voice_label",
    "vibe_flow_label",
    "vibe_peer_label",
    "vibe_peer_send",
    "vibe_peer_pager",
    "vibe_codex_pet",
    "vibe_rgb",
    "vibe_snake_autoplay",
    "vibe_2048_game",
    "vibe_breakout_game",
    "vibe_thunder_wing",
    "vibe_imu_lab",
    "vibe_pomodoro",
    "vibe_weather_pet",
    "vibe_audio_play",
    "vibe_audio_tone",
    "vibe_audio_stop",
    "vibe_audio_volume",
    "vibe_audio_label",
    "vibe_audio_tone_button",
    "vibe_audio_stop_button",
}

LUA_FORBIDDEN_CALLS = {
    "wifi_start",
    "http_get",
    "http_post",
    "i2s_start",
    "native_load",
}
LUA_FORBIDDEN_GLOBAL_ACCESS = re.compile(r"\b(?:os|io|debug|package)\s*[\.:]")


def fail(message: str, code: int = 1) -> None:
    if getattr(_ERROR_MODE, "raise_errors", False):
        raise RuntimePackageError(message)
    print(message, file=sys.stderr)
    sys.exit(code)


def safe_package_id(value: str | None) -> str:
    if not value or not SAFE_APP_ID.match(value):
        fail(f"Unsafe runtime app id: {value!r}")
    return value


def safe_package_path(value: str) -> str:
    value = value.replace("\\", "/")
    if value.startswith("/") or ".." in value or "//" in value or not SAFE_PATH.match(value):
        fail(f"Unsafe package path: {value!r}")
    return value


def is_esp32_native_capability(value: str) -> bool:
    normalized = value.strip().lower()
    return normalized in ESP32_NATIVE_CAPABILITY_NAMES or normalized.startswith(ESP32_NATIVE_CAPABILITY_PREFIXES)


def validate_huangshan_capability(value: str, context: str, allowed: set[str] | None = None) -> None:
    if not value or not isinstance(value, str):
        fail(f"{context} must be a non-empty string")
    if is_esp32_native_capability(value):
        fail(
            f"{context} {value!r} is not supported by Huangshan Runtime profile; "
            "use BLE/serial plus a phone or desktop bridge instead"
        )
    if allowed is not None and value not in allowed:
        fail(f"{context} {value!r} is not supported by Huangshan Runtime profile")


def validate_manifest_profile(data: dict[str, object]) -> None:
    for key in MANIFEST_PROFILE_FIELDS:
        if key not in data:
            continue
        value = data.get(key)
        if value is None:
            continue
        if not isinstance(value, str):
            fail(f"manifest.json {key} must be a string when present")
        if value.strip().lower() not in HUANGSHAN_PROFILE_ALIASES:
            fail(f"manifest.json {key} {value!r} is not compatible with Huangshan Runtime profile")


def validate_manifest_capability_lists(data: dict[str, object]) -> None:
    for key in MANIFEST_CAPABILITY_LIST_FIELDS:
        value = data.get(key)
        if value is None:
            continue
        if not isinstance(value, list):
            fail(f"manifest.json {key} must be a list of capability strings when present")
        for index, item in enumerate(value, start=1):
            if not isinstance(item, str):
                fail(f"manifest.json {key}[{index}] must be a capability string")
            validate_huangshan_capability(item, f"manifest.json {key}[{index}]", MANIFEST_DECLARED_CAPABILITIES)


def validate_manifest_metadata_string(data: dict[str, object], key: str, max_len: int) -> None:
    value = data.get(key)
    if value is None:
        return
    if not isinstance(value, str):
        fail(f"manifest.json {key} must be a string when present")
    if len(value) > max_len:
        fail(f"manifest.json {key} must be at most {max_len} characters")
    if any(ord(ch) < 32 for ch in value):
        fail(f"manifest.json {key} must not contain control characters")
    if key == "screenshot":
        if value.startswith("generated:"):
            return
        if not re.match(r"^(?:assets|images)/[A-Za-z0-9_./-]+\.(?:png|jpg|jpeg)$", value):
            fail("manifest.json screenshot must be generated:<name> or an assets/images PNG/JPEG path")


def validate_manifest_metadata(data: dict[str, object]) -> None:
    for key, max_len in MANIFEST_METADATA_STRING_FIELDS.items():
        validate_manifest_metadata_string(data, key, max_len)

    requirements = data.get("requirements")
    if requirements is None:
        return
    if not isinstance(requirements, list):
        fail("manifest.json requirements must be a list of strings when present")
    if len(requirements) > MANIFEST_REQUIREMENTS_MAX:
        fail(f"manifest.json requirements supports at most {MANIFEST_REQUIREMENTS_MAX} entries")
    for index, item in enumerate(requirements, start=1):
        if not isinstance(item, str):
            fail(f"manifest.json requirements[{index}] must be a string")
        if not item.strip():
            fail(f"manifest.json requirements[{index}] must not be empty")
        if len(item) > MANIFEST_REQUIREMENT_MAX_LEN:
            fail(f"manifest.json requirements[{index}] must be at most {MANIFEST_REQUIREMENT_MAX_LEN} characters")
        if any(ord(ch) < 32 for ch in item):
            fail(f"manifest.json requirements[{index}] must not contain control characters")
        if is_esp32_native_capability(item):
            fail(
                f"manifest.json requirements[{index}] {item!r} is not supported by Huangshan Runtime profile; "
                "use BLE/serial plus a phone or desktop bridge instead"
            )


def validate_manifest_component(component: dict[str, object], index: int) -> None:
    type_value = component.get("type", "status")
    if not isinstance(type_value, str):
        fail(f"manifest.json component #{index} type must be a string")
    if type_value not in MANIFEST_COMPONENT_TYPES:
        fail(f"manifest.json component #{index} type {type_value!r} is not supported")

    if type_value == "label":
        if not isinstance(component.get("text"), str):
            fail(f"manifest.json component #{index} text must be a string for label")
        for key in ("x", "y", "w"):
            value = component.get(key)
            if value is not None and (not isinstance(value, int) or isinstance(value, bool)):
                fail(f"manifest.json component #{index} {key} must be an integer for label")
        font = component.get("font")
        if font is not None and (not isinstance(font, int) or isinstance(font, bool) or int(font) <= 0):
            fail(f"manifest.json component #{index} font must be a positive integer for label")
        color = component.get("color")
        if color is not None and (not isinstance(color, str) or not HEX_COLOR.match(color)):
            fail(f"manifest.json component #{index} color must be #RRGGBB for label")
        return

    capability = component.get("capability")
    if not isinstance(capability, str):
        fail(f"manifest.json component #{index} capability must be a string for type {type_value!r}")
    validate_huangshan_capability(capability, f"manifest.json component #{index} capability", MANIFEST_CAPABILITIES)
    if capability not in MANIFEST_CAPABILITIES:
        fail(f"manifest.json component #{index} capability {capability!r} is not supported by Runtime manifest UI")
    label = component.get("label")
    if label is not None and not isinstance(label, str):
        fail(f"manifest.json component #{index} label must be a string")
    value = component.get("value")
    if value is not None and not isinstance(value, str):
        fail(f"manifest.json component #{index} value must be a string")
    if type_value == "action" and not isinstance(label, str):
        fail(f"manifest.json component #{index} label must be a string for action")


def validate_manifest(package_id: str, manifest_bytes: bytes) -> None:
    try:
        data = json.loads(manifest_bytes.decode("utf-8"))
    except UnicodeDecodeError as exc:
        fail(f"manifest.json must be UTF-8: {exc}")
    except json.JSONDecodeError as exc:
        fail(f"manifest.json is not valid JSON: {exc}")
    if not isinstance(data, dict):
        fail("manifest.json must contain a JSON object")

    version = data.get("schemaVersion", data.get("version"))
    if not isinstance(version, int) or isinstance(version, bool) or version != 1:
        fail(f"manifest.json must declare schemaVersion/version 1, got {version!r}")
    if data.get("kind") != "huangshan-runtime-app-manifest":
        fail(f"manifest.json kind must be 'huangshan-runtime-app-manifest', got {data.get('kind')!r}")
    if data.get("id") != package_id:
        fail(f"manifest.json id must match package id {package_id!r}, got {data.get('id')!r}")
    if data.get("entry", "main.lua") != "main.lua":
        fail(f"manifest.json entry must be 'main.lua', got {data.get('entry')!r}")
    validate_manifest_profile(data)
    validate_manifest_capability_lists(data)
    validate_manifest_metadata(data)

    components = data.get("components")
    if components is not None:
        if not isinstance(components, list):
            fail("manifest.json components must be a list when present")
        if len(components) > 8:
            fail(f"manifest.json supports at most 8 components, got {len(components)}")
        for index, component in enumerate(components, start=1):
            if not isinstance(component, dict):
                fail(f"manifest.json component #{index} must be an object")
            validate_manifest_component(component, index)


def manifest_file_entries(files: dict[str, bytes]) -> list[dict[str, object]]:
    return [
        {
            "path": path,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
        for path, data in sorted(files.items())
        if path != "manifest.json"
    ]


def digest_manifest_files(file_entries: list[dict[str, object]]) -> str:
    canonical = [
        {
            "path": entry["path"],
            "size": entry["size"],
            "sha256": entry["sha256"],
        }
        for entry in file_entries
    ]
    payload = json.dumps(canonical, ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b"\n"
    return hashlib.sha256(payload).hexdigest()


def with_package_integrity(package_id: str, files: dict[str, bytes]) -> dict[str, bytes]:
    if "manifest.json" not in files:
        return dict(sorted(files.items()))
    data = json.loads(files["manifest.json"].decode("utf-8"))
    entries = manifest_file_entries(files)
    data["files"] = entries
    data["integrity"] = {
        "algorithm": "sha256",
        "filesDigest": digest_manifest_files(entries),
    }
    manifest_data = json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8") + b"\n"
    validate_manifest(package_id, manifest_data)
    updated = dict(files)
    updated["manifest.json"] = manifest_data
    return dict(sorted(updated.items()))


def strip_lua_comment(line: str) -> str:
    in_string: str | None = None
    escaped = False
    index = 0
    while index < len(line):
        ch = line[index]
        if in_string:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == in_string:
                in_string = None
        elif ch in {"'", '"'}:
            in_string = ch
        elif ch == "-" and index + 1 < len(line) and line[index + 1] == "-":
            return line[:index].rstrip()
        index += 1
    return line.strip()


def lua_call_name(statement: str) -> str | None:
    value = statement.strip()
    if value.startswith("local "):
        _, _, rhs = value.partition("=")
        value = rhs.strip()
    match = re.match(rf"^({LUA_NAME})\s*\(", value)
    return match.group(1) if match else None


def validate_lua_subset(script_bytes: bytes) -> None:
    try:
        script = script_bytes.decode("utf-8")
    except UnicodeDecodeError as exc:
        fail(f"main.lua must be UTF-8: {exc}")
    if "\x00" in script:
        fail("main.lua must not contain NUL bytes")
    if len(script_bytes) > 64 * 1024:
        fail("main.lua must be at most 65536 bytes")
    if LUA_FORBIDDEN_GLOBAL_ACCESS.search(script):
        fail("main.lua accesses a standard library disabled by Huangshan Runtime")
    for name in LUA_FORBIDDEN_CALLS:
        if re.search(rf"\b{re.escape(name)}\s*\(", script):
            fail(f"main.lua calls forbidden Runtime Lua function {name!r}")


def validate_package(package_id: str, files: dict[str, bytes]) -> tuple[str, dict[str, bytes]]:
    package_id = safe_package_id(package_id)
    safe_files: dict[str, bytes] = {}
    for path, data in files.items():
        safe_path = safe_package_path(path)
        if safe_path in safe_files:
            fail(f"Duplicate package path after normalization: {safe_path!r}")
        if not isinstance(data, (bytes, bytearray)):
            fail(f"Runtime package file {safe_path!r} must be bytes")
        safe_files[safe_path] = bytes(data)
    if "main.lua" not in safe_files:
        fail("Runtime package must include main.lua")
    if "manifest.json" not in safe_files and "app.info" not in safe_files:
        fail("Runtime package must include manifest.json or app.info")
    validate_lua_subset(safe_files["main.lua"])
    if "manifest.json" in safe_files:
        validate_manifest(package_id, safe_files["manifest.json"])
    return package_id, with_package_integrity(package_id, safe_files)


def load_package_from_dir(package_dir: Path, app_id: str | None) -> tuple[str, dict[str, bytes]]:
    package_dir = package_dir.resolve()
    if not package_dir.is_dir():
        fail(f"Package directory does not exist: {package_dir}")
    package_id = safe_package_id(app_id or package_dir.name)
    files: dict[str, bytes] = {}
    seen: set[str] = set()
    for path in sorted(package_dir.rglob("*")):
        if not path.is_file():
            continue
        raw_rel = path.relative_to(package_dir).as_posix()
        rel = safe_package_path(raw_rel)
        if rel in seen:
            fail(f"Duplicate package path after normalization: {rel!r}")
        seen.add(rel)
        files[raw_rel] = path.read_bytes()
    return validate_package(package_id, files)


def load_package_from_json(package_json: Path, app_id: str | None) -> tuple[str, dict[str, bytes]]:
    data = json.loads(package_json.read_text(encoding="utf-8"))
    package_id = safe_package_id(
        app_id or data.get("app", {}).get("packageId") or data.get("app", {}).get("appId")
    )
    raw_files = data.get("files") or {}
    if not isinstance(raw_files, dict):
        fail("Runtime package JSON must contain a files object")
    files: dict[str, bytes] = {}
    seen: set[str] = set()
    for path, contents in raw_files.items():
        safe_path = safe_package_path(path)
        if safe_path in seen:
            fail(f"Duplicate package path after normalization: {safe_path!r}")
        seen.add(safe_path)
        files[path] = str(contents if contents is not None else "").encode("utf-8")
    return validate_package(package_id, files)


def build_install_commands(package_id: str, files: dict[str, bytes], chunk_bytes: int) -> list[str]:
    commands = [f"vb_runtime_install_begin {package_id}"]
    for path, data in sorted(files.items()):
        if not data:
            commands.append(f"vb_runtime_install_file {package_id} {path} 0 -")
            continue
        offset = 0
        while offset < len(data):
            prefix = f"vb_runtime_install_file {package_id} {path} {offset} "
            safe_chunk_bytes = min(chunk_bytes, (INSTALL_COMMAND_MAX_CHARS - len(prefix)) // 2)
            if safe_chunk_bytes <= 0:
                raise RuntimePackageError(f"Runtime package path is too long for serial install: {path!r}")
            chunk = data[offset : offset + safe_chunk_bytes]
            commands.append(prefix + chunk.hex())
            offset += len(chunk)
    commands.append(f"vb_runtime_install_end {package_id}")
    return commands


def validate_package_dir(package_dir: Path, app_id: str | None = None) -> tuple[str, dict[str, bytes]]:
    package_id, files = load_package_from_dir(package_dir, app_id)
    return package_id, files


def manifest_bytes(app_id: str = "test_app", **overrides: object) -> bytes:
    data: dict[str, object] = {
        "schemaVersion": 1,
        "kind": "huangshan-runtime-app-manifest",
        "id": app_id,
        "entry": "main.lua",
    }
    data.update(overrides)
    return json.dumps(data, ensure_ascii=False).encode("utf-8")


def package_files(app_id: str = "test_app", **manifest_overrides: object) -> dict[str, bytes]:
    return {
        "main.lua": b"print('ok')\n",
        "manifest.json": manifest_bytes(app_id, **manifest_overrides),
    }


def run_self_test() -> int:
    passed = 0

    def expect_ok(label: str, fn) -> None:
        nonlocal passed
        try:
            fn()
        except SystemExit as exc:
            fail(f"self-test {label} unexpectedly failed with SystemExit {exc.code!r}")
        except Exception as exc:  # pragma: no cover - defensive for direct CLI self-test
            fail(f"self-test {label} unexpectedly raised {type(exc).__name__}: {exc}")
        passed += 1

    def expect_fail(label: str, fn, needle: str) -> None:
        nonlocal passed
        stdout = io.StringIO()
        stderr = io.StringIO()
        try:
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                fn()
        except SystemExit as exc:
            if exc.code == 0:
                fail(f"self-test {label} exited successfully but should fail")
            output = stdout.getvalue() + stderr.getvalue()
            if needle not in output:
                fail(f"self-test {label} failed with wrong message; expected {needle!r}, got {output!r}")
            passed += 1
            return
        except Exception as exc:  # pragma: no cover - defensive for direct CLI self-test
            fail(f"self-test {label} raised {type(exc).__name__} instead of SystemExit: {exc}")
        fail(f"self-test {label} unexpectedly passed")

    expect_ok("valid manifest package", lambda: validate_package("test_app", package_files()))

    def generated_manifest_integrity() -> None:
        package_id, files = validate_package(
            "test_app",
            {
                "main.lua": b"print('ok')\n",
                "manifest.json": manifest_bytes("test_app"),
                "assets/note.txt": b"hello",
            },
        )
        manifest = json.loads(files["manifest.json"].decode("utf-8"))
        entries = manifest.get("files")
        if package_id != "test_app" or not isinstance(entries, list):
            fail("self-test generated integrity manifest missing files[]")
        by_path = {entry["path"]: entry for entry in entries}
        if "manifest.json" in by_path or "main.lua" not in by_path or "assets/note.txt" not in by_path:
            fail(f"self-test generated integrity manifest paths wrong: {by_path!r}")
        if by_path["main.lua"]["sha256"] != hashlib.sha256(b"print('ok')\n").hexdigest():
            fail("self-test generated integrity manifest main.lua hash mismatch")
        if manifest.get("integrity", {}).get("filesDigest") != digest_manifest_files(entries):
            fail("self-test generated integrity manifest filesDigest mismatch")

    expect_ok("generated manifest integrity", generated_manifest_integrity)
    expect_ok(
        "valid power package",
        lambda: validate_package(
            "test_app",
            {
                "main.lua": b"local l = lv_label_create(lv_scr_act())\nvibe_power_label(l, 'charger')\n",
                "manifest.json": manifest_bytes(
                    components=[{"type": "status", "capability": "power.charger", "label": "Charger"}]
                ),
            },
        ),
    )
    expect_ok(
        "valid display package",
        lambda: validate_package(
            "test_app",
            {
                "main.lua": b"local l = lv_label_create(lv_scr_act())\nvibe_display_brightness('70')\nvibe_display_label(l, 'brightness')\n",
                "manifest.json": manifest_bytes(
                    components=[{"type": "status", "capability": "display.brightness", "label": "Brightness"}]
                ),
            },
        ),
    )
    expect_ok(
        "valid voice package",
        lambda: validate_package(
            "test_app",
            {
                "main.lua": b"local l = lv_label_create(lv_scr_act())\nvibe_voice_start('600')\nvibe_voice_stop()\nvibe_voice_clear()\nvibe_voice_label(l, 'ready')\n",
                "manifest.json": manifest_bytes(
                    components=[
                        {"type": "status", "capability": "voice.ready", "label": "Voice"},
                        {"type": "action", "capability": "voice.start", "label": "Record", "value": "600"},
                        {"type": "action", "capability": "voice.stop", "label": "Finish"},
                        {"type": "action", "capability": "voice.clear", "label": "Clear"},
                    ]
                ),
            },
        ),
    )
    expect_ok(
        "valid flow package",
        lambda: validate_package(
            "test_app",
            {
                "main.lua": b"local l = lv_label_create(lv_scr_act())\nvibe_flow_label(l, 'latest')\n",
                "manifest.json": manifest_bytes(
                    components=[{"type": "status", "capability": "flow.latest", "label": "Flow"}]
                ),
            },
        ),
    )
    expect_ok(
        "valid peer pager package",
        lambda: validate_package(
            "test_app",
            {
                "main.lua": b"vibe_peer_pager()\n",
                "manifest.json": manifest_bytes(
                    runtimeProfile="huangshan-pi",
                    capabilities=["peer", "peer.status", "peer.messages", "peer.send", "peer.pair", "voice", "voice.start", "voice.stop", "flow"],
                    components=[],
                ),
            },
        ),
    )
    expect_ok(
        "valid app metadata package",
        lambda: validate_package(
            "test_app",
            package_files(
                category="Games",
                icon="gamepad-2",
                author="Huangshan Runtime Team",
                screenshot="assets/preview.png",
                requirements=["Runtime", "Touch gestures"],
            ),
        ),
    )
    expect_ok("legacy app.info package", lambda: validate_package("legacy_app", {"main.lua": b"", "app.info": b"legacy_app"}))

    expect_fail("bad app id", lambda: validate_package("BadApp", package_files("bad_app")), "Unsafe runtime app id")
    expect_fail("unsafe path", lambda: validate_package("test_app", {"main.lua": b"", "../evil.lua": b"x", "app.info": b"x"}), "Unsafe package path")
    expect_fail(
        "duplicate normalized path",
        lambda: validate_package(
            "test_app",
            {"main.lua": b"", "app.info": b"x", "assets/icon.png": b"1", "assets\\icon.png": b"2"},
        ),
        "Duplicate package path",
    )
    expect_fail("missing main", lambda: validate_package("test_app", {"manifest.json": manifest_bytes()}), "main.lua")
    expect_fail("missing manifest", lambda: validate_package("test_app", {"main.lua": b""}), "manifest.json or app.info")
    expect_fail("manifest utf8", lambda: validate_package("test_app", {"main.lua": b"", "manifest.json": b"\xff"}), "UTF-8")
    expect_fail("manifest json", lambda: validate_package("test_app", {"main.lua": b"", "manifest.json": b"{"}), "not valid JSON")
    expect_fail("manifest object", lambda: validate_package("test_app", {"main.lua": b"", "manifest.json": b"[]"}), "JSON object")
    expect_fail("schema bool", lambda: validate_package("test_app", package_files(schemaVersion=True)), "schemaVersion/version 1")
    expect_fail("schema string", lambda: validate_package("test_app", package_files(schemaVersion="1")), "schemaVersion/version 1")
    expect_fail("kind", lambda: validate_package("test_app", package_files(kind="wrong")), "kind")
    expect_fail("id mismatch", lambda: validate_package("test_app", package_files("other_app")), "id must match")
    expect_fail("entry", lambda: validate_package("test_app", package_files(entry="boot.lua")), "entry")
    expect_ok("huangshan profile", lambda: validate_package("test_app", package_files(runtimeProfile="huangshan-pi", capabilities=["display", "ble", "flow.latest"])))
    expect_fail("wrong profile", lambda: validate_package("test_app", package_files(runtimeProfile="esp32")), "not compatible with Huangshan Runtime profile")
    expect_fail("capabilities type", lambda: validate_package("test_app", package_files(capabilities="display")), "must be a list")
    expect_fail("capabilities item type", lambda: validate_package("test_app", package_files(capabilities=[7])), "must be a capability string")
    expect_fail("native wifi capability", lambda: validate_package("test_app", package_files(capabilities=["wifi"])), "BLE/serial plus a phone or desktop bridge")
    expect_fail("native http capability", lambda: validate_package("test_app", package_files(requires=["http.client"])), "BLE/serial plus a phone or desktop bridge")
    expect_fail("native camera permission", lambda: validate_package("test_app", package_files(permissions=["camera"])), "BLE/serial plus a phone or desktop bridge")
    expect_fail("native module capability", lambda: validate_package("test_app", package_files(capabilities=["native"])), "BLE/serial plus a phone or desktop bridge")
    expect_fail("gamepad permission", lambda: validate_package("test_app", package_files(permissions=["gamepad"])), "BLE/serial plus a phone or desktop bridge")
    expect_fail("nes capability", lambda: validate_package("test_app", package_files(requires=["nes"])), "BLE/serial plus a phone or desktop bridge")
    expect_fail("i2s capability", lambda: validate_package("test_app", package_files(capabilities=["i2s.audio"])), "BLE/serial plus a phone or desktop bridge")
    expect_fail("unknown declared capability", lambda: validate_package("test_app", package_files(capabilities=["esp32.psram"])), "not supported by Huangshan Runtime profile")
    expect_fail("metadata category type", lambda: validate_package("test_app", package_files(category=7)), "category must be a string")
    expect_fail("metadata screenshot path", lambda: validate_package("test_app", package_files(screenshot="../preview.png")), "screenshot")
    expect_fail("requirements type", lambda: validate_package("test_app", package_files(requirements="Runtime")), "requirements must be a list")
    expect_fail("requirements item type", lambda: validate_package("test_app", package_files(requirements=[7])), "requirements[1] must be a string")
    expect_fail("requirements native wifi", lambda: validate_package("test_app", package_files(requirements=["wifi"])), "BLE/serial plus a phone or desktop bridge")
    expect_fail("components list", lambda: validate_package("test_app", package_files(components={})), "components must be a list")
    expect_fail("components count", lambda: validate_package("test_app", package_files(components=[{}] * 9)), "at most 8")
    expect_fail("component object", lambda: validate_package("test_app", package_files(components=["bad"])), "must be an object")
    expect_fail("component type", lambda: validate_package("test_app", package_files(components=[{"type": 7}])), "type must be a string")
    expect_fail("component capability", lambda: validate_package("test_app", package_files(components=[{"type": "status"}])), "capability")
    expect_fail("action label", lambda: validate_package("test_app", package_files(components=[{"type": "action", "capability": "reload"}])), "label")
    expect_fail("component unknown type", lambda: validate_package("test_app", package_files(components=[{"type": "chart", "text": "x"}])), "type 'chart' is not supported")
    expect_fail("component unknown capability", lambda: validate_package("test_app", package_files(components=[{"type": "status", "capability": "wifi.rssi", "label": "Wi-Fi"}])), "capability 'wifi.rssi' is not supported")
    expect_fail("label text", lambda: validate_package("test_app", package_files(components=[{"type": "label", "text": 7}])), "text must be a string")
    expect_fail("label color", lambda: validate_package("test_app", package_files(components=[{"type": "label", "text": "x", "color": "blue"}])), "color must be #RRGGBB")
    expect_fail("label x", lambda: validate_package("test_app", package_files(components=[{"type": "label", "text": "x", "x": True}])), "x must be an integer")
    expect_fail("label font", lambda: validate_package("test_app", package_files(components=[{"type": "label", "text": "x", "font": 0}])), "font must be a positive integer")
    expect_fail("component value", lambda: validate_package("test_app", package_files(components=[{"type": "status", "capability": "status", "label": "Status", "value": 1}])), "value must be a string")
    expect_fail("lua utf8", lambda: validate_package("test_app", {"main.lua": b"\xff", "app.info": b"test_app"}), "main.lua must be UTF-8")
    expect_ok("full lua control flow", lambda: validate_package("test_app", {"main.lua": b"local total=0\nfor i=1,3 do total=total+i end\nprint(total)\n", "app.info": b"test_app"}))
    expect_ok("full lua assignment", lambda: validate_package("test_app", {"main.lua": b"root = lv_scr_act()\n", "app.info": b"test_app"}))
    expect_ok("full lua app-local require", lambda: validate_package("test_app", {"main.lua": b"local m=require('lib.demo')\nprint(m.value)\n", "lib/demo.lua": b"return {value=42}\n", "app.info": b"test_app"}))
    expect_fail("lua forbidden call", lambda: validate_package("test_app", {"main.lua": b"wifi_start()\n", "app.info": b"test_app"}), "forbidden Runtime Lua function")
    expect_fail("lua disabled standard library", lambda: validate_package("test_app", {"main.lua": b"os.execute('bad')\n", "app.info": b"test_app"}), "standard library disabled")

    def duplicate_json_package() -> None:
        payload = {
            "app": {"packageId": "test_app"},
            "files": {
                "main.lua": "",
                "app.info": "test_app",
                "assets/icon.png": "1",
                "assets\\icon.png": "2",
            },
        }
        with tempfile.TemporaryDirectory(prefix="runtime-package-test-") as temp_dir:
            package_json = Path(temp_dir) / "package.json"
            package_json.write_text(json.dumps(payload), encoding="utf-8")
            load_package_from_json(package_json, None)

    expect_fail("package json duplicate path", duplicate_json_package, "Duplicate package path")

    def duplicate_dir_package() -> None:
        with tempfile.TemporaryDirectory(prefix="runtime-package-dir-test-") as temp_dir:
            package_dir = Path(temp_dir) / "test_app"
            assets_dir = package_dir / "assets"
            package_dir.mkdir()
            assets_dir.mkdir()
            (package_dir / "main.lua").write_text("", encoding="utf-8")
            (package_dir / "app.info").write_text("test_app", encoding="utf-8")
            (assets_dir / "icon.png").write_bytes(b"1")
            (package_dir / "assets\\icon.png").write_bytes(b"2")
            load_package_from_dir(package_dir, None)

    expect_fail("package dir duplicate path", duplicate_dir_package, "Duplicate package path")

    def bounded_install_commands() -> None:
        path = "assets/pets/long_pet_name/blocked.bin"
        original = bytes(range(256)) * 3
        commands = build_install_commands("test_app", {path: original}, 240)
        file_commands = commands[1:-1]
        if not file_commands or max(map(len, file_commands)) > INSTALL_COMMAND_MAX_CHARS:
            raise RuntimePackageError("install command exceeded FinSH input limit")
        rebuilt = b"".join(bytes.fromhex(command.rsplit(" ", 1)[1]) for command in file_commands)
        if rebuilt != original:
            raise RuntimePackageError("bounded install commands changed file contents")

    expect_ok("install commands fit FinSH line", bounded_install_commands)

    def package_error_mode_is_thread_local() -> None:
        entered = threading.Event()
        release = threading.Event()
        results: list[tuple[str, object, object]] = []
        lock = threading.Lock()

        def record(kind: str, value: object, detail: object = "") -> None:
            with lock:
                results.append((kind, value, detail))

        def exception_worker() -> None:
            try:
                with package_errors_as_exceptions():
                    entered.set()
                    try:
                        safe_package_id("BadApp")
                    except RuntimePackageError as exc:
                        record("exception", type(exc).__name__, str(exc))
                    else:
                        record("exception", "missing", "safe_package_id unexpectedly passed")
                    release.wait(2.0)
            except Exception as exc:  # pragma: no cover - defensive for thread failures
                record("exception", type(exc).__name__, str(exc))

        def cli_worker() -> None:
            if not entered.wait(2.0):
                record("cli", "timeout", "exception worker did not enter context")
                return
            stderr = io.StringIO()
            try:
                with contextlib.redirect_stderr(stderr):
                    safe_package_id("BadApp")
            except SystemExit as exc:
                record("cli", exc.code, stderr.getvalue())
            except Exception as exc:  # pragma: no cover - defensive for thread failures
                record("cli", type(exc).__name__, str(exc))
            finally:
                release.set()

        threads = [
            threading.Thread(target=exception_worker),
            threading.Thread(target=cli_worker),
        ]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=3.0)
        if any(thread.is_alive() for thread in threads):
            fail("self-test exception mode thread-local timed out")
        if not any(kind == "exception" and value == "RuntimePackageError" for kind, value, _ in results):
            fail(f"self-test exception mode did not raise RuntimePackageError in context: {results!r}")
        if not any(kind == "cli" and value != 0 and "Unsafe runtime app id" in str(detail) for kind, value, detail in results):
            fail(f"self-test exception mode leaked into CLI thread: {results!r}")

    expect_ok("package exception mode is thread-local", package_error_mode_is_thread_local)

    print(f"runtime_package self-test ok: {passed} cases")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Huangshan Runtime app packages without installing them.")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--package-dir", type=Path, help="Validate one Runtime app package directory.")
    source.add_argument("--package-json", type=Path, help="Validate one Runtime package JSON file.")
    source.add_argument("--all", action="store_true", help="Validate every package directory under --root.")
    source.add_argument("--self-test", action="store_true", help="Run built-in positive and negative package validator checks.")
    parser.add_argument("--app-id", help="Override package id for --package-dir or --package-json.")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent / "runtime_apps")
    args = parser.parse_args()

    results: list[tuple[str, str, int, int]] = []
    if args.self_test:
        return run_self_test()
    if args.all:
        if not args.root.is_dir():
            fail(f"Runtime app root does not exist: {args.root}")
        for package_dir in sorted(path for path in args.root.iterdir() if path.is_dir()):
            package_id, files = validate_package_dir(package_dir)
            results.append((package_dir.name, package_id, len(files), sum(len(data) for data in files.values())))
    elif args.package_dir:
        package_id, files = validate_package_dir(args.package_dir, args.app_id)
        results.append((args.package_dir.name, package_id, len(files), sum(len(data) for data in files.values())))
    elif args.package_json:
        package_id, files = load_package_from_json(args.package_json, args.app_id)
        results.append((args.package_json.name, package_id, len(files), sum(len(data) for data in files.values())))

    for label, package_id, file_count, byte_count in results:
        print(f"ok {label}: app={package_id} files={file_count} bytes={byte_count}")
    print(f"validated runtime packages: {len(results)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
