#!/usr/bin/env python3
from __future__ import annotations

import re

from runtime_transport import (
    CAPABILITIES_API,
    DISPLAY_API,
    GPIO_API,
    POWER_API,
    RGB_API,
    SENSORS_API as SENSOR_API,
    TOUCH_API,
    VOICE_API,
)


RUNTIME_API = "vibeboard-huangshan-runtime/v1"
RGB_COLOR_RE = re.compile(r"^[0-9a-fA-F]{6}$")
GPIO_PINS = {
    "key1": 34,
    "key2": 43,
}


def validate_capabilities_json(
    data: dict[str, object] | None,
    require_gpio: bool,
    require_ble: bool = False,
) -> str | None:
    if not data:
        return "missing capabilities JSON"
    if data.get("api") != CAPABILITIES_API:
        return f"capabilities api mismatch: {data.get('api')!r}"
    if data.get("rt") != RUNTIME_API:
        return f"runtime api mismatch: {data.get('rt')!r}"

    ins = data.get("ins")
    if not isinstance(ins, dict):
        return "capabilities.ins missing"
    if ins.get("ser") != 1:
        return f"capabilities.ins.ser expected 1, got {ins.get('ser')!r}"
    if require_ble and ins.get("ble") != 1:
        return f"capabilities.ins.ble expected 1, got {ins.get('ble')!r}"
    if not isinstance(ins.get("max"), int) or int(ins["max"]) < 48:
        return f"capabilities.ins.max too small: {ins.get('max')!r}"

    app = data.get("app")
    if not isinstance(app, dict):
        return "capabilities.app missing"
    if not isinstance(app.get("lua"), str) or not app.get("lua"):
        return f"capabilities.app.lua invalid: {app.get('lua')!r}"
    if not isinstance(app.get("comp"), int) or int(app["comp"]) < 1:
        return f"capabilities.app.comp invalid: {app.get('comp')!r}"

    hw = data.get("hw")
    if not isinstance(hw, dict):
        return "capabilities.hw missing"
    for key in ("disp", "touch", "flow"):
        if hw.get(key) != 1:
            return f"capabilities.hw.{key} expected 1, got {hw.get(key)!r}"

    if data.get("disp") != DISPLAY_API:
        return f"capabilities display api mismatch: {data.get('disp')!r}"

    if require_gpio:
        if data.get("gpio") != GPIO_API:
            return f"capabilities gpio api mismatch: {data.get('gpio')!r}"
        if hw.get("gpio") != 1:
            return f"capabilities.hw.gpio expected 1, got {hw.get('gpio')!r}"

    return None


def validate_power_json(data: dict[str, object] | None) -> str | None:
    if not data:
        return "missing power JSON"
    if data.get("api") != POWER_API:
        return f"power api mismatch: {data.get('api')!r}"
    if data.get("available") != 1:
        return f"power.available expected 1, got {data.get('available')!r}"
    if data.get("ready") != 1:
        return f"power.ready expected 1, got {data.get('ready')!r}"

    battery = data.get("battery")
    if not isinstance(battery, dict):
        return "power.battery missing"
    if battery.get("ok") != 1:
        return f"power.battery.ok expected 1, got {battery.get('ok')!r}"
    if not isinstance(battery.get("mv"), int) or int(battery["mv"]) <= 0:
        return f"power.battery.mv invalid: {battery.get('mv')!r}"
    if not isinstance(battery.get("raw"), int) or int(battery["raw"]) <= 0:
        return f"power.battery.raw invalid: {battery.get('raw')!r}"
    if not isinstance(battery.get("dev"), str) or not battery.get("dev"):
        return f"power.battery.dev invalid: {battery.get('dev')!r}"
    if not isinstance(battery.get("ch"), int) or int(battery["ch"]) < 0:
        return f"power.battery.ch invalid: {battery.get('ch')!r}"

    charger = data.get("charger")
    if not isinstance(charger, dict):
        return "power.charger missing"
    if charger.get("ok") != 1:
        return f"power.charger.ok expected 1, got {charger.get('ok')!r}"
    if not isinstance(charger.get("status"), str) or not charger.get("status"):
        return f"power.charger.status invalid: {charger.get('status')!r}"
    for key in ("state", "det", "en", "sys", "fault"):
        value = charger.get(key)
        if value is None or not isinstance(value, int):
            return f"power.charger.{key} invalid: {value!r}"

    return None


def validate_display_json(data: dict[str, object] | None, expected_brightness: int | None = None) -> str | None:
    if not data:
        return "missing display JSON"
    if data.get("api") != DISPLAY_API:
        return f"display api mismatch: {data.get('api')!r}"
    if data.get("available") != 1:
        return f"display.available expected 1, got {data.get('available')!r}"
    if data.get("ready") != 1:
        return f"display.ready expected 1, got {data.get('ready')!r}"
    if data.get("ok") != 1:
        return f"display.ok expected 1, got {data.get('ok')!r}"
    if not isinstance(data.get("dev"), str) or not data.get("dev"):
        return f"display.dev invalid: {data.get('dev')!r}"
    for key in ("width", "height", "bpp"):
        value = data.get(key)
        if not isinstance(value, int) or int(value) <= 0:
            return f"display.{key} invalid: {value!r}"
    for key in ("format", "align", "state"):
        value = data.get(key)
        if value is None or not isinstance(value, int):
            return f"display.{key} invalid: {value!r}"
    brightness = data.get("brightness")
    if not isinstance(brightness, int) or int(brightness) < 0 or int(brightness) > 100:
        return f"display.brightness invalid: {brightness!r}"
    if expected_brightness is not None and int(brightness) != expected_brightness:
        return f"display.brightness expected {expected_brightness}, got {brightness!r}"
    if not isinstance(data.get("state_name"), str) or not data.get("state_name"):
        return f"display.state_name invalid: {data.get('state_name')!r}"
    return None


def validate_touch_json(data: dict[str, object] | None) -> str | None:
    if not data:
        return "missing touch JSON"
    if data.get("api") != TOUCH_API:
        return f"touch api mismatch: {data.get('api')!r}"
    if data.get("available") != 1:
        return f"touch.available expected 1, got {data.get('available')!r}"
    if data.get("ready") != 1:
        return f"touch.ready expected 1, got {data.get('ready')!r}"
    if data.get("active") not in (0, 1):
        return f"touch.active expected 0/1, got {data.get('active')!r}"
    if not isinstance(data.get("count"), int) or int(data["count"]) < 0:
        return f"touch.count invalid: {data.get('count')!r}"
    for key in ("x", "y", "dx", "dy", "tick", "duration_ms"):
        value = data.get(key)
        if value is None or not isinstance(value, int):
            return f"touch.{key} invalid: {value!r}"
    if not isinstance(data.get("event"), str) or not data.get("event"):
        return f"touch.event invalid: {data.get('event')!r}"
    if not isinstance(data.get("gesture"), str) or not data.get("gesture"):
        return f"touch.gesture invalid: {data.get('gesture')!r}"

    return None


def validate_gpio_json(data: dict[str, object] | None) -> str | None:
    if not data:
        return "missing gpio JSON"
    if data.get("api") != GPIO_API:
        return f"gpio api mismatch: {data.get('api')!r}"
    if data.get("available") != 1:
        return f"gpio.available expected 1, got {data.get('available')!r}"
    if data.get("ready") != 1:
        return f"gpio.ready expected 1, got {data.get('ready')!r}"
    if data.get("inputs_only") != 1:
        return f"gpio.inputs_only expected 1, got {data.get('inputs_only')!r}"
    if not isinstance(data.get("count"), int) or int(data["count"]) < len(GPIO_PINS):
        return f"gpio.count too small: {data.get('count')!r}"

    for name, pin in GPIO_PINS.items():
        item = data.get(name)
        if not isinstance(item, dict):
            return f"gpio.{name} missing"
        if item.get("ok") != 1:
            return f"gpio.{name}.ok expected 1, got {item.get('ok')!r}"
        if item.get("pin") != pin:
            return f"gpio.{name}.pin expected {pin}, got {item.get('pin')!r}"
        if item.get("active_high") != 1:
            return f"gpio.{name}.active_high expected 1, got {item.get('active_high')!r}"
        if item.get("level") not in (0, 1):
            return f"gpio.{name}.level expected 0/1, got {item.get('level')!r}"
        if item.get("pressed") not in (0, 1):
            return f"gpio.{name}.pressed expected 0/1, got {item.get('pressed')!r}"

    return None


def normalize_rgb_color(value: object) -> str | None:
    if not isinstance(value, str) or not RGB_COLOR_RE.fullmatch(value):
        return None
    return value.lower()


def validate_sensors_json(data: dict[str, object] | None) -> str | None:
    if not data:
        return "missing sensors JSON"
    if data.get("api") != SENSOR_API:
        return f"sensors api mismatch: {data.get('api')!r}"
    if data.get("available") != 1:
        return f"sensors.available expected 1, got {data.get('available')!r}"
    if not isinstance(data.get("count"), int) or int(data["count"]) < 0:
        return f"sensors.count invalid: {data.get('count')!r}"

    active_count = 0
    sensor_specs = (
        ("light", ("lux",)),
        ("mag", ("x", "y", "z")),
        ("acce", ("x", "y", "z")),
        ("gyro", ("x", "y", "z")),
        ("step", ("count",)),
    )
    for name, keys in sensor_specs:
        item = data.get(name)
        if not isinstance(item, dict):
            return f"sensors.{name} missing"
        ok = item.get("ok")
        if ok not in (0, 1):
            return f"sensors.{name}.ok expected 0/1, got {ok!r}"
        active_count += int(ok)
        for key in keys:
            value = item.get(key)
            if value is None or not isinstance(value, int):
                return f"sensors.{name}.{key} invalid: {value!r}"

    if data.get("count") != active_count:
        return f"sensors.count expected {active_count}, got {data.get('count')!r}"
    expected_ready = 1 if active_count > 0 else 0
    if data.get("ready") != expected_ready:
        return f"sensors.ready expected {expected_ready}, got {data.get('ready')!r}"
    if active_count <= 0:
        return "sensors expected at least one online sensor"
    return None


def validate_rgb_json(data: dict[str, object] | None, expected_color: str | None = None) -> str | None:
    if not data:
        return "missing rgb JSON"
    if data.get("api") != RGB_API:
        return f"rgb api mismatch: {data.get('api')!r}"
    if data.get("available") != 1:
        return f"rgb.available expected 1, got {data.get('available')!r}"
    if data.get("ready") != 1:
        return f"rgb.ready expected 1, got {data.get('ready')!r}"
    if data.get("ok") != 1:
        return f"rgb.ok expected 1, got {data.get('ok')!r}"
    if not isinstance(data.get("dev"), str) or not data.get("dev"):
        return f"rgb.dev invalid: {data.get('dev')!r}"
    if not isinstance(data.get("count"), int) or int(data["count"]) < 1:
        return f"rgb.count invalid: {data.get('count')!r}"
    actual_color = normalize_rgb_color(data.get("color"))
    if not actual_color:
        return f"rgb.color invalid: {data.get('color')!r}"
    if expected_color is not None and actual_color != normalize_rgb_color(expected_color):
        return f"rgb.color expected {normalize_rgb_color(expected_color)!r}, got {actual_color!r}"
    if not isinstance(data.get("name"), str) or not data.get("name"):
        return f"rgb.name invalid: {data.get('name')!r}"
    return None



def validate_voice_json(data: dict[str, object] | None) -> str | None:
    if not data:
        return "missing voice JSON"
    if data.get("api") != VOICE_API:
        return f"voice api mismatch: {data.get('api')!r}"
    if data.get("available") not in (0, 1):
        return f"voice.available expected 0/1, got {data.get('available')!r}"
    if data.get("built") not in (0, 1):
        return f"voice.built expected 0/1, got {data.get('built')!r}"
    if data.get("available") != data.get("built"):
        return f"voice.available/built mismatch: {data.get('available')!r}/{data.get('built')!r}"
    if data.get("ready") not in (0, 1):
        return f"voice.ready expected 0/1, got {data.get('ready')!r}"
    if data.get("recording") not in (0, 1):
        return f"voice.recording expected 0/1, got {data.get('recording')!r}"
    for key in ("seq", "requested_ms", "bytes", "dropped", "err"):
        value = data.get(key)
        if value is None or not isinstance(value, int):
            return f"voice.{key} invalid: {value!r}"
    if int(data["seq"]) < 0 or int(data["requested_ms"]) < 0 or int(data["bytes"]) < 0 or int(data["dropped"]) < 0:
        return "voice counters must be non-negative"
    expected_audio = {"rate": 16000, "bits": 16, "channels": 1}
    for key, expected in expected_audio.items():
        value = data.get(key)
        if value != expected:
            return f"voice.{key} expected {expected}, got {value!r}"
    return None


def format_section_outputs(sections: list[tuple[str, str]]) -> str:
    lines: list[str] = []
    for label, text in sections:
        lines.append(f"[{label}]")
        body = text.rstrip()
        if body:
            lines.append(body)
    return "\n".join(lines)
def _base_capabilities() -> dict[str, object]:
    return {
        "api": CAPABILITIES_API,
        "rt": RUNTIME_API,
        "ble": "vibeboard-huangshan-ble-install/v1",
        "sens": SENSOR_API,
        "touch": TOUCH_API,
        "flow": "vibeboard-huangshan-info-flow/v1",
        "voice": VOICE_API,
        "pwr": POWER_API,
        "disp": DISPLAY_API,
        "gpio": GPIO_API,
        "rgb": RGB_API,
        "fs": 1,
        "ins": {"ser": 1, "ble": 1, "max": 240},
        "app": {"lua": "script-subset", "comp": 8},
        "hw": {"disp": 1, "touch": 1, "sens": 1, "voice": 1, "flow": 1, "batt": 1, "chg": 1, "gpio": 1, "rgb": 1},
    }


def _base_power() -> dict[str, object]:
    return {
        "api": POWER_API,
        "available": 1,
        "ready": 1,
        "battery": {"ok": 1, "mv": 3900, "raw": 1234, "dev": "bat1", "ch": 0},
        "charger": {"ok": 1, "status": "idle", "state": 0, "det": 1, "en": 1, "sys": 0, "fault": 0},
    }


def _base_display() -> dict[str, object]:
    return {
        "api": DISPLAY_API,
        "available": 1,
        "ready": 1,
        "ok": 1,
        "dev": "lcd",
        "width": 410,
        "height": 502,
        "bpp": 16,
        "format": 0,
        "align": 4,
        "state": 1,
        "brightness": 70,
        "state_name": "on",
    }


def _base_touch() -> dict[str, object]:
    return {
        "api": TOUCH_API,
        "available": 1,
        "ready": 1,
        "active": 0,
        "count": 0,
        "x": 0,
        "y": 0,
        "dx": 0,
        "dy": 0,
        "tick": 1,
        "duration_ms": 0,
        "event": "idle",
        "gesture": "none",
    }


def _base_gpio() -> dict[str, object]:
    return {
        "api": GPIO_API,
        "available": 1,
        "ready": 1,
        "inputs_only": 1,
        "count": 2,
        "key1": {"ok": 1, "pin": 34, "active_high": 1, "level": 0, "pressed": 0},
        "key2": {"ok": 1, "pin": 43, "active_high": 1, "level": 0, "pressed": 0},
    }


def _base_sensors() -> dict[str, object]:
    return {
        "api": SENSOR_API,
        "available": 1,
        "ready": 1,
        "count": 1,
        "light": {"ok": 1, "lux": 123},
        "mag": {"ok": 0, "x": 0, "y": 0, "z": 0},
        "acce": {"ok": 0, "x": 0, "y": 0, "z": 0},
        "gyro": {"ok": 0, "x": 0, "y": 0, "z": 0},
        "step": {"ok": 0, "count": 0},
    }


def _base_rgb() -> dict[str, object]:
    return {
        "api": RGB_API,
        "available": 1,
        "ready": 1,
        "ok": 1,
        "dev": "rgb",
        "count": 1,
        "color": "3366ff",
        "name": "status",
    }


def _base_voice() -> dict[str, object]:
    return {
        "api": VOICE_API,
        "available": 1,
        "built": 1,
        "ready": 1,
        "recording": 0,
        "seq": 1,
        "requested_ms": 800,
        "bytes": 1024,
        "dropped": 0,
        "err": 0,
        "rate": 16000,
        "bits": 16,
        "channels": 1,
    }


def run_self_test() -> None:
    assert validate_capabilities_json(_base_capabilities(), require_gpio=True, require_ble=True) is None
    no_ble = _base_capabilities()
    assert isinstance(no_ble["ins"], dict)
    no_ble["ins"] = dict(no_ble["ins"])
    no_ble["ins"].pop("ble")
    assert validate_capabilities_json(no_ble, require_gpio=False) is None
    assert "ins.ble" in (validate_capabilities_json(no_ble, require_gpio=False, require_ble=True) or "")

    assert validate_power_json(_base_power()) is None
    assert validate_display_json(_base_display(), expected_brightness=70) is None
    assert validate_touch_json(_base_touch()) is None
    assert validate_gpio_json(_base_gpio()) is None
    assert validate_sensors_json(_base_sensors()) is None
    assert validate_rgb_json(_base_rgb(), expected_color="3366ff") is None
    assert validate_voice_json(_base_voice()) is None

    bad_rgb = _base_rgb()
    bad_rgb["color"] = "blue"
    assert "rgb.color" in (validate_rgb_json(bad_rgb) or "")
    bad_voice = _base_voice()
    bad_voice["rate"] = 8000
    assert "voice.rate" in (validate_voice_json(bad_voice) or "")
    assert normalize_rgb_color("A1b2C3") == "a1b2c3"
    assert normalize_rgb_color("not-rgb") is None
    assert format_section_outputs([("one", "a\n"), ("two", "")]) == "[one]\na\n[two]"
    print("runtime_reliability_checks self-test ok")


if __name__ == "__main__":
    run_self_test()
