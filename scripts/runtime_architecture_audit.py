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


def audit_direct_transport_usage() -> None:
    allowed_direct_serial = {
        "scripts/runtime_transport.py",
        "scripts/flash.py",
        "scripts/monitor.sh",
        "scripts/monitor.ps1",
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
        ok("direct_transport_usage", "direct pyserial use is limited to RuntimeTransport/flash/monitor diagnostics")
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
    audit_high_risk_capability_evaluation()
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
