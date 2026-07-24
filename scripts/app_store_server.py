#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ast
import asyncio
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import mimetypes
import uuid
import urllib.parse
import urllib.request
import webbrowser
import zipfile
import struct
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Awaitable, Callable, TypeVar

from runtime_app_plan_writer import RuntimeAppPlanError, build_app_plan_package
from runtime_package import RuntimePackageError, load_package_from_dir, package_errors_as_exceptions, safe_package_id, safe_package_path, validate_package
from runtime_transport import (
    BLETransport,
    BLETransportOptions,
    DEFAULT_DEVICE_NAME,
    INSTALL_CHUNK_BYTES,
    RUNTIME_DATA_CHUNK_BYTES,
    RuntimeTransportError,
    SyncRuntimeTransport,
    AsyncRuntimeTransport,
    ensure_bleak,
    SerialTransport,
    SerialTransportOptions,
    parse_key_values,
)


ROOT_DIR = Path(__file__).resolve().parents[1]
RUNTIME_APPS_DIR = ROOT_DIR / "scripts" / "runtime_apps"
FLASH_SCRIPT = ROOT_DIR / "scripts" / "flash.py"
PAGER_VOICE_ACTIVE_FILE = Path.home() / ".vibeboard" / "pager-voice-active"
APP_ORDER = ["thunder_wing", "imu_lab", "breakout", "jump_jump", "pomodoro", "weather_pet", "game_2048", "auto_snake", "sensor_stage"]
DEFAULT_CITY = "Nanchang Honggutan"
DEFAULT_LATITUDE = 28.6986
DEFAULT_LONGITUDE = 115.8582
TRANSPORT_KIND = "ble"
SERIAL_PORT: str | None = None
SERIAL_BAUD = 1_000_000
BLE_NAME = DEFAULT_DEVICE_NAME
BLE_NO_CACHE = False
TRANSPORT_LOCK = threading.Lock()
SERIAL_INSTALL_CHUNK_BYTES = INSTALL_CHUNK_BYTES
BLE_INSTALL_CHUNK_BYTES = RUNTIME_DATA_CHUNK_BYTES
IMPORT_MAX_BYTES = 4 * 1024 * 1024
JOB_LOG_MAX_LINES = 260
T = TypeVar("T")


WEATHER_CODE_CONDITION = {
    0: "sunny",
    1: "sunny",
    2: "cloudy",
    3: "cloudy",
    45: "fog",
    48: "fog",
    51: "rain",
    53: "rain",
    55: "rain",
    56: "rain",
    57: "rain",
    61: "rain",
    63: "rain",
    65: "rain",
    66: "rain",
    67: "rain",
    71: "snow",
    73: "snow",
    75: "snow",
    77: "snow",
    80: "rain",
    81: "rain",
    82: "rain",
    85: "snow",
    86: "snow",
    95: "storm",
    96: "storm",
    99: "storm",
}


WEATHER_ASSETS = {"sunny", "cloudy", "rain", "snow", "storm", "fog"}


def request_path(raw_path: str) -> str:
    path = urllib.parse.urlsplit(raw_path).path
    return path or "/"


def run_self_test() -> None:
    cases = {
        "/": "/",
        "/index.html?cache=0": "/index.html",
        "/api/apps?refresh=1": "/api/apps",
        "/api/runtime/capabilities?refresh=1": "/api/runtime/capabilities",
        "/api/runtime/apps/clock_test/launch?from=ui": "/api/runtime/apps/clock_test/launch",
        "/api/jobs/job%201?poll=1": "/api/jobs/job%201",
    }
    for raw, expected in cases.items():
        actual = request_path(raw)
        if actual != expected:
            raise SystemExit(f"request_path({raw!r}) expected {expected!r}, got {actual!r}")
    if not is_transport_exception(RuntimeTransportError("boom")):
        raise SystemExit("RuntimeTransportError should be treated as a transport exception")
    if not is_transport_exception(asyncio.TimeoutError("timeout")):
        raise SystemExit("asyncio.TimeoutError should be treated as a transport exception")
    if is_transport_exception(ValueError("bad request")):
        raise SystemExit("ValueError should remain a client/request error")
    if SERIAL_INSTALL_CHUNK_BYTES > INSTALL_CHUNK_BYTES:
        raise SystemExit("serial install chunks must stay within the conservative MSH line budget")
    if BLE_INSTALL_CHUNK_BYTES < SERIAL_INSTALL_CHUNK_BYTES:
        raise SystemExit("BLE install chunks should be at least as large as serial chunks")
    apps = list_apps()
    if not apps:
        raise SystemExit("App Store should expose local apps")
    required_keys = {"category", "icon", "author", "screenshot", "requirements"}
    missing = required_keys - set(apps[0])
    if missing:
        raise SystemExit(f"App Store app entries missing metadata keys: {sorted(missing)}")
    if not isinstance(apps[0]["requirements"], list):
        raise SystemExit("App Store requirements should be normalized to a list")
    if not app_screenshot_url("demo_app", "generated:demo").endswith("/generated-screenshot.png"):
        raise SystemExit("generated screenshot references should be served as preview images")
    asset_url = app_screenshot_url("weather_pet", "assets/weather/cloudy.png")
    if not asset_url.startswith("/api/apps/weather_pet/asset/"):
        raise SystemExit(f"asset screenshot should be served through app asset API, got {asset_url}")
    runtime_payload = normalize_runtime_app_screenshots({
        "apps": [
            {"id": "weather_pet", "screenshot": "assets/weather/cloudy.png"},
            {"id": "third_party", "screenshot": "assets/private.png"},
        ]
    })
    runtime_shots = [item.get("screenshot") for item in runtime_payload["apps"]]
    if not str(runtime_shots[0]).startswith("/api/apps/weather_pet/asset/"):
        raise SystemExit("Runtime App Manager should resolve curated package screenshots")
    if runtime_shots[1] != "generated:third_party":
        raise SystemExit("unknown Runtime app screenshots should use a safe generated fallback")
    matrix = matrix_from_capabilities({"hw": {"disp": 1, "touch": 1, "flow": 1}, "disp": "display/v1", "touch": "touch/v1", "flow": "flow/v1", "ins": {"ser": 1, "ble": 1}})
    if len(matrix) != 8 or not matrix[0]["available"]:
        raise SystemExit("capability matrix should expose all advertised Runtime rows")
    exported_id, exported_files = export_package_files("game_2048")
    if exported_id != "game_2048":
        raise SystemExit(f"exported game_2048 package id mismatch: {exported_id}")
    for required in ("manifest.json", "main.lua", "README.md", "images/screenshot.png"):
        if required not in exported_files:
            raise SystemExit(f"exported package missing {required}")
    imported_id, imported_files, screenshot = package_from_archive(zip_package_bytes(exported_id, exported_files))
    if imported_id != exported_id or screenshot != "images/screenshot.png" or "README.md" not in imported_files:
        raise SystemExit("exported package should round-trip through import validation")
    plan_result = build_app_plan_package({
        "app": {
            "id": "plan_demo",
            "name": "Plan Demo",
            "description": "Generated from Web App Store plan JSON.",
            "capabilities": ["display"],
        },
        "components": [{"type": "label", "text": "Plan OK", "color": "#5eead4"}],
    })
    if plan_result.app_id != "plan_demo" or "manifest.json" not in plan_result.files:
        raise SystemExit("app-plan-writer should generate a validated package")
    plan_manifest = json.loads(plan_result.files["manifest.json"].decode("utf-8"))
    if not isinstance(plan_manifest.get("files"), list) or not plan_manifest.get("integrity", {}).get("filesDigest"):
        raise SystemExit("app-plan-writer packages should include manifest integrity")
    source = Path(__file__).read_text(encoding="utf-8")
    tree = ast.parse(source)
    function_names = {node.name for node in ast.walk(tree) if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))}
    if "run_transport_operation" not in function_names:
        raise SystemExit("App Store bridge should expose one RuntimeTransport runner")
    forbidden_names = {"run_ble_install", "run_serial_install"}
    forbidden_prefixes = ("read_ble_runtime_",)
    for name in sorted(function_names):
        if name in forbidden_names or any(name.startswith(prefix) for prefix in forbidden_prefixes):
            raise SystemExit(f"App Store bridge should not keep per-endpoint transport helper: {name}")

    global BLE_NAME, BLE_NO_CACHE
    previous_ble_name = BLE_NAME
    previous_ble_no_cache = BLE_NO_CACHE
    try:
        BLE_NAME = "TestBoard"
        BLE_NO_CACHE = True
        options = ble_options()
        if options.name != "TestBoard" or options.no_cache is not True:
            raise SystemExit("ble_options should honor App Store bridge BLE CLI settings")
        if options.response_wait != 0.2 or options.final_wait != 0.8:
            raise SystemExit("ble_options should preserve App Store bridge timing defaults")
    finally:
        BLE_NAME = previous_ble_name
        BLE_NO_CACHE = previous_ble_no_cache
    test_state = StoreState()
    first_job = test_state.start_job("test", "demo", lambda job: None, request_key="request-1")
    repeated_job = test_state.start_job("test", "demo", lambda job: None, request_key="request-1")
    if repeated_job is not first_job:
        raise SystemExit("repeated install request IDs should return the original job")
    print("app_store_server self-test ok")


APP_META = {
    "thunder_wing": {
        "summary": "左右倾斜控制原创战机，倾角越大飞得越快。击破波次并挑战阶段首领。",
        "accent": "#38bdf8",
        "requiresWeather": False,
    },
    "imu_lab": {
        "summary": "红色姿态点会随板子倾斜，在电子水平表盘中实时移动。",
        "accent": "#5db7ff",
        "requiresWeather": False,
    },
    "breakout": {
        "summary": "拖动霓虹挡板，弹回小球并清空整面砖墙。",
        "accent": "#47d7ac",
        "requiresWeather": False,
    },
    "jump_jump": {
        "summary": "按住蓄力、松手起跳，落在平台中心 +2 分的霓虹跳一跳。",
        "accent": "#5db7ff",
        "requiresWeather": False,
    },
    "pomodoro": {
        "summary": "会在专注与休息间自动切换的触控番茄钟。",
        "accent": "#ff5c65",
        "requiresWeather": False,
    },
    "weather_pet": {
        "summary": "真实天气生成的可爱动画天气角色。",
        "accent": "#fbbf24",
        "requiresWeather": True,
    },
    "game_2048": {
        "summary": "经典 2048 小棋盘，用来验证 BLE 安装和启动流程。",
        "accent": "#f97316",
        "requiresWeather": False,
    },
    "auto_snake": {
        "summary": "自动跑的贪吃蛇，安装后会自己移动和计分。",
        "accent": "#22c55e",
        "requiresWeather": False,
    },
    "sensor_stage": {
        "summary": "板载光照、运动、步数和时钟的展示台。",
        "accent": "#38bdf8",
        "requiresWeather": False,
    },
}

ICON_GLYPHS = {
    "rocket": "T",
    "axis-3d": "I",
    "brick-wall": "B",
    "cloud-sun": "W",
    "gamepad-2": "G",
    "activity": "S",
    "bot": "A",
    "sparkles": "*",
    "mic": "V",
    "hand": "T",
    "battery": "P",
    "monitor": "D",
    "keyboard": "K",
    "radio": "F",
    "circle": "R",
    "clock": "C",
    "app": "A",
}


HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="cache-control" content="no-store">
  <title>VibeBoard App Store</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #101418;
      --panel: #17212b;
      --panel-2: #1f2a35;
      --line: #334155;
      --text: #eef4f8;
      --muted: #9fb0bd;
      --ok: #35d49f;
      --warn: #fbbf24;
      --bad: #fb7185;
      --blue: #5db7ff;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      background: linear-gradient(135deg, #101418 0%, #18222b 48%, #11181d 100%);
      color: var(--text);
      font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      letter-spacing: 0;
    }
    main { width: min(1180px, calc(100vw - 48px)); margin: 0 auto; padding: 28px 0 36px; }
    header {
      display: flex;
      align-items: flex-end;
      justify-content: space-between;
      gap: 20px;
      margin-bottom: 22px;
    }
    h1 { margin: 0; font-size: 34px; line-height: 1.08; font-weight: 750; }
    .subtitle { margin-top: 8px; color: var(--muted); font-size: 15px; }
    .statusbar {
      display: flex;
      align-items: center;
      gap: 10px;
      min-width: 300px;
      max-width: min(58vw, 640px);
      justify-content: flex-end;
      color: var(--muted);
      font-size: 14px;
    }
    #transportText { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .dot { width: 10px; height: 10px; border-radius: 50%; background: var(--warn); box-shadow: 0 0 18px currentColor; }
    .dot.ok { background: var(--ok); }
    .dot.bad { background: var(--bad); }
    .nav {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      margin: 0 0 18px;
    }
    .nav button {
      min-height: 34px;
      padding: 0 13px;
      color: var(--muted);
      background: #0d141a;
      border: 1px solid var(--line);
    }
    .nav button.active { color: #081117; background: var(--blue); border-color: var(--blue); }
    .page { display: none; }
    .page.active { display: block; }
    .topgrid {
      display: grid;
      grid-template-columns: 1.2fr 0.8fr;
      gap: 16px;
      margin-bottom: 18px;
    }
    .panel, .app {
      background: color-mix(in srgb, var(--panel) 94%, black);
      border: 1px solid color-mix(in srgb, var(--line) 78%, transparent);
      border-radius: 8px;
      box-shadow: 0 18px 48px rgba(0, 0, 0, 0.22);
    }
    .panel { padding: 18px; }
    .panel h2 { margin: 0 0 12px; font-size: 17px; }
    .panel-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 16px;
      margin-bottom: 18px;
    }
    .panel-grid.three { grid-template-columns: repeat(3, minmax(0, 1fr)); }
    .dropzone {
      display: grid;
      gap: 10px;
      min-height: 128px;
      place-items: center;
      text-align: center;
      border: 1px dashed color-mix(in srgb, var(--blue) 60%, var(--line));
      border-radius: 8px;
      background: #0d141a;
      color: var(--muted);
      padding: 18px;
    }
    .dropzone.drag { border-color: var(--ok); color: #d1fae5; background: #10231f; }
    .dropzone input { display: none; }
    .import-result, .diagnostic-card, .cap-row {
      background: #111a22;
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 12px;
    }
    .kv { display: grid; gap: 7px; color: var(--muted); font-size: 13px; }
    .kv b { color: var(--text); }
    .weather-row { display: grid; grid-template-columns: 1fr 1fr auto; gap: 10px; align-items: end; }
    label { display: block; color: var(--muted); font-size: 12px; margin-bottom: 6px; }
    input, textarea {
      width: 100%;
      border-radius: 6px;
      border: 1px solid var(--line);
      background: #0d141a;
      color: var(--text);
      font-size: 14px;
    }
    input { height: 38px; padding: 0 11px; }
    textarea {
      min-height: 220px;
      resize: vertical;
      padding: 11px;
      font: 12px/1.5 ui-monospace, SFMono-Regular, Menlo, monospace;
    }
    button {
      border: 0;
      border-radius: 6px;
      min-height: 38px;
      padding: 0 14px;
      color: #081117;
      background: var(--blue);
      font-weight: 700;
      cursor: pointer;
      transition: transform 0.12s ease, opacity 0.12s ease;
    }
    button.secondary { background: #2dd4bf; }
    button:disabled { opacity: 0.45; cursor: wait; }
    button:active { transform: translateY(1px); }
    .hint { margin-top: 10px; color: var(--muted); font-size: 13px; min-height: 18px; }
    .filters { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
    .filter {
      min-height: 30px;
      padding: 0 11px;
      border: 1px solid var(--line);
      background: #0d141a;
      color: var(--muted);
      border-radius: 999px;
      font-weight: 700;
    }
    .filter.active { color: #081117; background: var(--blue); border-color: var(--blue); }
    .apps {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 16px;
    }
    .app { padding: 0; position: relative; overflow: hidden; }
    .app::before {
      content: "";
      position: absolute;
      inset: 0 0 auto;
      height: 4px;
      background: var(--accent, var(--blue));
    }
    .preview {
      height: 196px;
      padding: 18px;
      display: grid;
      place-items: center;
      background: linear-gradient(135deg, color-mix(in srgb, var(--accent, var(--blue)) 28%, #0d141a), #0d141a 72%);
      border-bottom: 1px solid var(--line);
    }
    .preview img { width: 100%; height: 160px; object-fit: contain; border-radius: 6px; }
    .generated-shot {
      width: min(160px, 70%);
      aspect-ratio: 1;
      border-radius: 8px;
      display: grid;
      place-items: center;
      background: color-mix(in srgb, var(--accent, var(--blue)) 38%, #111a22);
      color: #f8fafc;
      font-size: 52px;
      font-weight: 800;
      border: 1px solid color-mix(in srgb, white 16%, transparent);
      box-shadow: inset 0 0 34px rgba(255,255,255,0.08);
    }
    .app-body { padding: 16px; }
    .app h3 { margin: 6px 0 6px; font-size: 20px; line-height: 1.15; }
    .app p { margin: 0 0 14px; color: var(--muted); min-height: 42px; font-size: 14px; line-height: 1.45; }
    .app .meta, .app .byline { display: flex; align-items: center; justify-content: space-between; gap: 10px; color: var(--muted); font-size: 12px; }
    .app .byline { margin-bottom: 12px; }
    .badge { border: 1px solid var(--line); border-radius: 999px; padding: 4px 9px; }
    .reqs { display: flex; flex-wrap: wrap; gap: 6px; min-height: 28px; margin-bottom: 14px; }
    .req {
      display: inline-flex;
      align-items: center;
      min-height: 22px;
      border: 1px solid color-mix(in srgb, var(--accent, var(--blue)) 45%, var(--line));
      color: #dbeafe;
      border-radius: 999px;
      padding: 4px 8px 2px;
      font-size: 11px;
      line-height: 1;
    }
    .app button { width: 100%; background: var(--accent, var(--blue)); }
    .app-actions { display: grid; grid-template-columns: 1fr auto; gap: 8px; }
    .app-actions .export { width: auto; min-width: 74px; background: #e2e8f0; color: #0f172a; }
    .section-head { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin: 24px 0 12px; }
    .section-head h2 { margin: 0; font-size: 19px; }
    .runtime-list { display: grid; gap: 16px; }
    .runtime-note { padding: 10px 12px; background: #0d141a; border: 1px solid var(--line); border-radius: 8px; color: var(--muted); font-size: 13px; }
    .runtime-note.warn { border-color: color-mix(in srgb, var(--warn) 60%, var(--line)); color: #fde68a; }
    .runtime-card { display: grid; grid-template-columns: 96px minmax(0, 1fr) auto; gap: 14px; align-items: stretch; padding: 12px; background: #111a22; border: 1px solid var(--line); border-radius: 8px; }
    .runtime-card.active { border-color: color-mix(in srgb, var(--accent, var(--blue)) 72%, var(--line)); background: color-mix(in srgb, var(--accent, var(--blue)) 10%, #111a22); }
    .runtime-shot { min-height: 88px; border-radius: 6px; display: grid; place-items: center; background: color-mix(in srgb, var(--accent, var(--blue)) 24%, #0d141a); overflow: hidden; }
    .runtime-shot img { width: 100%; height: 100%; object-fit: cover; }
    .runtime-shot .generated-shot { width: 64px; font-size: 28px; }
    .runtime-card .badge { padding: 2px 7px; font-size: 11px; line-height: 1.2; }
    .runtime-name { font-weight: 750; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: 16px; }
    .runtime-desc { color: var(--muted); font-size: 13px; line-height: 1.35; margin-top: 5px; }
    .runtime-state { color: var(--muted); font-size: 12px; margin-top: 8px; }
    .runtime-actions { display: flex; gap: 8px; justify-content: flex-end; }
    .runtime-actions button { min-height: 32px; padding: 0 10px; }
    button.danger { background: var(--bad); color: #22050b; }
    button.warn { background: var(--warn); color: #1f1300; }
    .diagnostic-card h3, .cap-row h3 { margin: 0 0 10px; font-size: 15px; }
    .checks { display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); gap: 10px; }
    .check { border: 1px solid var(--line); border-radius: 8px; padding: 10px; background: #0d141a; min-height: 74px; }
    .check.ok { border-color: color-mix(in srgb, var(--ok) 60%, var(--line)); }
    .check.bad { border-color: color-mix(in srgb, var(--bad) 60%, var(--line)); }
    .check-title { color: var(--text); font-weight: 750; font-size: 13px; }
    .check-body { color: var(--muted); font-size: 12px; margin-top: 6px; overflow-wrap: anywhere; }
    .cap-table { display: grid; gap: 10px; }
    .cap-row {
      display: grid;
      grid-template-columns: 120px 1fr 1fr 1fr auto;
      gap: 12px;
      align-items: center;
    }
    .cap-row .name { font-weight: 800; }
    .state-pill { border-radius: 999px; padding: 5px 9px; font-size: 12px; font-weight: 800; text-align: center; }
    .state-pill.ok { background: color-mix(in srgb, var(--ok) 22%, #0d141a); color: #bbf7d0; }
    .state-pill.bad { background: color-mix(in srgb, var(--bad) 18%, #0d141a); color: #fecdd3; }
    .log {
      margin-top: 18px;
      background: #070d12;
      border: 1px solid var(--line);
      border-radius: 8px;
      min-height: 190px;
      max-height: 300px;
      overflow: auto;
      padding: 12px;
      font: 12px/1.45 ui-monospace, SFMono-Regular, Menlo, monospace;
      color: #cbd5e1;
      white-space: pre-wrap;
    }
    .progress {
      height: 8px;
      background: #0d141a;
      border-radius: 999px;
      overflow: hidden;
      border: 1px solid var(--line);
    }
    .progress.is-idle { display: none; }
    .bar { height: 100%; width: 0%; background: var(--ok); transition: width 0.2s ease; }
    @media (max-width: 860px) {
      main { width: min(100vw - 28px, 680px); padding-top: 18px; }
      header, .topgrid { grid-template-columns: 1fr; display: grid; }
      .statusbar { justify-content: flex-start; min-width: 0; }
      .apps { grid-template-columns: 1fr; }
      .panel-grid, .panel-grid.three { grid-template-columns: 1fr; }
      .checks { grid-template-columns: 1fr; }
      .cap-row { grid-template-columns: 1fr; align-items: start; }
      .weather-row { grid-template-columns: 1fr; }
      .runtime-card { grid-template-columns: 76px minmax(0, 1fr); }
      .runtime-actions { grid-column: 1 / -1; }
      .runtime-actions { justify-content: stretch; }
      .runtime-actions button { flex: 1; }
      h1 { font-size: 28px; }
    }
  </style>
</head>
<body>
  <main>
    <header>
      <div>
        <h1>VibeBoard App Store</h1>
        <div class="subtitle">Mac mini 本地桥接到黄山派 Runtime</div>
      </div>
      <div class="statusbar"><span id="transportDot" class="dot"></span><span id="transportText">检查 Transport 中...</span></div>
    </header>

    <nav class="nav">
      <button data-page="store" class="active">App Store</button>
      <button data-page="recovery">Recovery</button>
      <button data-page="capabilities">Capabilities</button>
    </nav>

    <section id="page-store" class="page active">
      <section class="topgrid">
        <div class="panel">
          <h2>天气数据</h2>
          <div class="weather-row">
            <div>
              <label for="cityInput">城市兜底</label>
              <input id="cityInput" value="Nanchang Honggutan" autocomplete="address-level2">
            </div>
            <div>
              <label for="weatherText">当前天气</label>
              <input id="weatherText" readonly value="尚未获取">
            </div>
            <button class="secondary" id="weatherBtn">获取天气</button>
          </div>
          <div id="weatherHint" class="hint">优先用浏览器定位；失败时使用城市输入。</div>
        </div>
        <div class="panel">
          <h2>安装进度</h2>
          <div id="progressTrack" class="progress is-idle"><div id="progressBar" class="bar"></div></div>
          <div id="jobText" class="hint">等待选择 App。</div>
          <div id="capabilityHint" class="hint">等待 Runtime 能力握手。</div>
        </div>
      </section>

      <section class="panel">
        <h2>App 包导入</h2>
        <label id="dropzone" class="dropzone" for="packageFile">
          <input id="packageFile" type="file" accept=".zip,.happ,application/zip">
          <strong>拖入 .zip / .happ 包</strong>
          <span>本地校验 manifest、截图、requirements 后再安装。</span>
        </label>
        <div id="importResult" class="hint">尚未导入包。</div>
      </section>

      <section class="panel">
        <h2>AI App Plan</h2>
        <textarea id="planText" spellcheck="false"></textarea>
        <div class="runtime-actions" style="margin-top:12px">
          <button id="planGenerate" class="secondary">生成并校验</button>
          <button id="planSample">填入示例</button>
        </div>
        <div id="planResult" class="hint">粘贴 JSON plan 后会生成一个本地 Runtime App 包，并进入上方导入安装流程。</div>
      </section>

      <div class="section-head">
        <h2>本地 App Store</h2>
        <div id="storeFilters" class="filters"></div>
      </div>
      <section id="apps" class="apps"></section>

      <div class="section-head">
        <h2>板上 App Manager</h2>
        <div class="runtime-actions">
          <button id="runtimeStop" class="danger">停止当前</button>
          <button id="runtimeRefresh" class="secondary">刷新</button>
        </div>
      </div>
      <section id="runtimeApps" class="runtime-list"></section>
    </section>

    <section id="page-recovery" class="page">
      <div class="section-head">
        <h2>Recovery Center</h2>
        <div class="runtime-actions">
          <button id="recoveryRefresh" class="secondary">重新检测</button>
        </div>
      </div>
      <section id="diagnosticSummary" class="checks"></section>
      <section class="panel-grid three">
        <div class="panel">
          <h2>设备状态</h2>
          <div id="recoveryRuntime" class="kv">等待检测。</div>
        </div>
        <div class="panel">
          <h2>已安装 App</h2>
          <div id="recoveryApps" class="kv">等待读取。</div>
        </div>
        <div class="panel">
          <h2>最近错误</h2>
          <div id="recoveryErrors" class="kv">等待读取。</div>
        </div>
      </section>
      <section class="panel">
        <h2>恢复操作</h2>
        <div class="runtime-actions">
          <button id="clearStaging" class="secondary">清理 staging</button>
          <button id="reinstallExamples" class="secondary">重装示例 App</button>
          <button id="flashRuntime" class="warn">一键恢复 Runtime</button>
        </div>
        <div id="recoveryHint" class="hint">恢复 Runtime 只在串口模式下可用，点击后需要二次确认。</div>
      </section>
    </section>

    <section id="page-capabilities" class="page">
      <div class="section-head">
        <h2>Capability Matrix</h2>
        <button id="capabilityRefresh" class="secondary">刷新矩阵</button>
      </div>
      <section id="capabilityMatrix" class="cap-table"></section>
    </section>

    <section id="log" class="log">ready
</section>
  </main>

  <script>
    const state = {
      apps: [],
      runtimeApps: [],
      capabilities: null,
      weather: null,
      installing: false,
      runtimeBusy: false,
      runtimeLoading: false,
      runtimeError: "",
      runtimeLastUpdated: "",
      runtimeActive: "",
      runtimeState: "",
      importedPackage: null,
      diagnostics: null,
      capabilityMatrix: [],
      currentPage: "store",
      storeCategory: "All"
    };
    const $ = id => document.getElementById(id);
    const esc = value => String(value ?? "").replace(/[&<>"']/g, ch => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#39;"
    }[ch]));
    const compactText = (value, limit = 180) => {
      const text = String(value ?? "").replace(/\s+/g, " ").trim();
      if (text.includes("Last output:")) return text.slice(0, text.indexOf("Last output:")).trim();
      return text.length > limit ? `${text.slice(0, limit - 3)}...` : text;
    };
    const log = message => {
      const box = $("log");
      const time = new Date().toLocaleTimeString();
      box.textContent += `[${time}] ${compactText(message, 240)}\n`;
      box.scrollTop = box.scrollHeight;
    };
    const statusLine = status => {
      const lines = String(status || "").split(/\r?\n/).map(line => line.trim()).filter(Boolean);
      const okLine = lines.find(line => /^(ok|err)\s/.test(line));
      const text = okLine || lines[lines.length - 1] || "done";
      return text.length > 160 ? `${text.slice(0, 157)}...` : text;
    };
    const installChunkText = (message, logText) => {
      if (message !== "uploading files") return message;
      const matches = [...String(logText || "").matchAll(/install_file\s+(\d+)\/(\d+)/g)];
      const last = matches[matches.length - 1];
      if (!last) return "正在通过串口上传...";
      const current = Number(last[1]);
      const total = Number(last[2]);
      const pct = total ? Math.min(99, Math.round(current / total * 100)) : 0;
      return `串口上传 ${current}/${total} (${pct}%)`;
    };
    const api = async (url, options = {}) => {
      const res = await fetch(url, {
        ...options,
        headers: { "content-type": "application/json", ...(options.headers || {}) }
      });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
      return data;
    };
    const apiUpload = async (url, file) => {
      const res = await fetch(url, {
        method: "POST",
        headers: { "content-type": "application/octet-stream" },
        body: file
      });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
      return data;
    };
    const createInstallJob = async (appId, payload) => {
      let lastError = null;
      for (let attempt = 1; attempt <= 2; attempt += 1) {
        try {
          return await api(`/api/apps/${appId}/install`, {
            method: "POST",
            body: JSON.stringify(payload)
          });
        } catch (err) {
          lastError = err;
          const transient = /failed to fetch|networkerror|load failed/i.test(String(err?.message || err));
          if (!transient || attempt === 2) throw err;
          setInstallProgress(4);
          $("jobText").textContent = "连接短暂中断，正在恢复安装请求...";
          await new Promise(resolve => setTimeout(resolve, 450));
        }
      }
      throw lastError || new Error("安装请求失败");
    };
    const setTransport = (ok, text) => {
      $("transportDot").className = `dot ${ok ? "ok" : "bad"}`;
      $("transportText").textContent = text;
      $("transportText").title = text;
    };
    const setInstallProgress = value => {
      $("progressTrack").classList.remove("is-idle");
      const width = Math.max(0, Math.min(100, Number(value) || 0));
      $("progressBar").style.width = `${width}%`;
    };
    const clearInstallProgress = () => {
      $("progressBar").style.width = "0%";
      $("progressTrack").classList.add("is-idle");
    };
    const displayAppName = appId => {
      const app = [...state.apps, ...state.runtimeApps].find(item => item.id === appId);
      return app?.name || appId || "App";
    };
    const runtimeConnectionSummary = runtimeState => {
      const value = String(runtimeState || "").toLowerCase();
      if (value === "running" || value === "1" || value === "true") return "已连接 · Runtime 运行中";
      if (value === "idle" || value === "0" || value === "false" || value === "failed") return "已连接 · Runtime 未运行";
      return "已连接";
    };
    const runtimeStatusValue = (status, key) => {
      const match = String(status || "").match(new RegExp(`\\[vb_runtime\\] ${key}=([^\\s\\r\\n]+)`));
      return match ? match[1] : "";
    };
    const transportSummary = data => {
      const raw = String(data?.status || "");
      if (!raw.includes("[vb_runtime]")) return "已连接";
      const fs = runtimeStatusValue(raw, "fs") || "unknown";
      const running = runtimeStatusValue(raw, "running");
      const parts = [runtimeConnectionSummary(running)];
      if (fs && fs !== "ready") parts.push("SD卡待检查");
      return parts.join(" · ");
    };
    const friendlyTransportError = (message, transport = "") => {
      const text = String(message || "").trim();
      const label = transport === "serial" ? "串口" : "BLE";
      if (/pyserial/i.test(text)) return "串口桥缺少 pyserial，请重启服务";
      if (/load failed|timed out|timeout|did not receive|not connected|disconnected/i.test(text)) {
        return `${label} 状态读取失败，请刷新重试`;
      }
      return text || `${label} 状态读取失败，请刷新重试`;
    };
    const friendlyRuntimeAppsError = message => {
      const text = String(message || "").trim();
      if (/load failed|timed out|timeout|did not receive|json response|not connected|disconnected/i.test(text)) {
        return "读取 App 列表超时，请刷新重试";
      }
      return compactText(text) || "读取 App 列表失败，请刷新重试";
    };
    const friendlyCapabilityError = message => {
      const text = String(message || "").trim();
      if (/pyserial/i.test(text)) return "串口桥缺少 pyserial，请用 SiFli Python 环境启动服务";
      if (/did not receive|json response|timed out|timeout|load failed|not connected|disconnected/i.test(text)) {
        return "Runtime 能力握手失败，请刷新重试";
      }
      return compactText(text) || "Runtime 能力握手失败，请刷新重试";
    };
    const friendlyWeatherError = message => {
      const text = String(message || "").trim();
      if (/urlopen|ssl|network|timed out|timeout|failed/i.test(text)) {
        return "天气暂时获取失败，可继续安装离线 App。";
      }
      return text || "天气暂时获取失败，可继续安装离线 App。";
    };
    const capabilitySummary = caps => {
      const hw = caps?.hw || {};
      const labels = { disp: "显示", touch: "触摸", sens: "传感器", voice: "语音", flow: "信息流", batt: "电池", chg: "充电", gpio: "KEY", rgb: "RGB" };
      const enabled = Object.keys(labels).filter(key => Number(hw[key]) === 1).map(key => labels[key]);
      const ins = caps?.ins || {};
      const installs = [Number(ins.ser) === 1 ? "串口" : "", Number(ins.ble) === 1 ? "BLE" : ""].filter(Boolean).join("/");
      return `Runtime ${caps?.api ? "已握手" : "能力未知"} · ${enabled.join(" / ") || "硬件待确认"} · 安装 ${installs || "待确认"}`;
    };
    const refreshTransport = async () => {
      try {
        const data = await api("/api/transport/status");
        const text = data.ok ? transportSummary(data) : friendlyTransportError(data.status || data.error, data.transport);
        setTransport(data.ok, text);
      } catch (err) {
        setTransport(false, friendlyTransportError(err.message));
      }
    };
    const refreshCapabilities = async () => {
      try {
        const data = await api("/api/runtime/capabilities");
        if (!data.ok) throw new Error(data.errorSummary || data.error || "Runtime capabilities unavailable");
        state.capabilities = data.capabilities;
        $("capabilityHint").textContent = capabilitySummary(state.capabilities);
        log(`capabilities ${state.capabilities.api || "unknown"}`);
      } catch (err) {
        const text = friendlyCapabilityError(err.message);
        $("capabilityHint").textContent = text;
        log(`capabilities failed: ${text}`);
      }
    };
    const conditionLabel = item => `${item.city || "Current"} ${Math.round(item.temperature_c)}C ${item.condition} RH ${Math.round(item.humidity)}%`;
    const runtimeAppsPayload = data => data.runtime?.apps || [];
    const runtimeActive = data => data.runtime?.active || "";
    const runtimeState = data => data.runtime?.state || "";
    const displayCategory = value => ({
      All: "全部",
      Companions: "伙伴",
      Games: "游戏",
      Hardware: "硬件",
      General: "本地包",
      local: "本地包"
    }[value] || value || "本地包");
    const localStoreMeta = appId => state.apps.find(item => item.id === appId) || {};
    const runtimeDisplayApp = app => {
      const meta = localStoreMeta(app.id);
      const hasMeta = Boolean(meta.id);
      const merged = { ...meta, ...app };
      merged.category = app.category || meta.category || "local";
      merged.author = app.author || meta.author || "本地包";
      merged.description = hasMeta
        ? (meta.summary || meta.description || app.description || "Runtime app package")
        : (app.description || "Runtime app package");
      merged.icon = app.icon || meta.icon || "app";
      merged.iconText = app.iconText || meta.iconText || iconText(merged);
      merged.screenshot = meta.screenshot || app.screenshot || `generated:${app.id}`;
      merged.requirements = app.requirements || meta.requirements || [];
      merged.accent = app.accent || meta.accent || "#5db7ff";
      return merged;
    };
    const requirements = app => {
      const value = app?.requirements;
      if (Array.isArray(value)) return value.filter(Boolean).map(String);
      return String(value || "").split(",").map(item => item.trim()).filter(Boolean);
    };
    const iconText = app => app?.iconText || String(app?.icon || app?.category || app?.id || "A").slice(0, 1).toUpperCase();
    const generatedPreview = app => `<div class="generated-shot">${esc(iconText(app))}</div>`;
    const preview = app => {
      const shot = String(app?.screenshot || "");
      if (shot && !shot.startsWith("generated:")) {
        return `<img src="${esc(shot)}" alt="">`;
      }
      return generatedPreview(app);
    };
    const categoryList = () => ["All", ...Array.from(new Set(state.apps.map(app => app.category || "General"))).sort()];
    const renderFilters = () => {
      $("storeFilters").innerHTML = categoryList().map(category => `
        <button class="filter ${category === state.storeCategory ? "active" : ""}" data-category="${esc(category)}">${esc(displayCategory(category))}</button>
      `).join("");
      document.querySelectorAll("[data-category]").forEach(button => {
        button.addEventListener("click", () => {
          state.storeCategory = button.dataset.category || "All";
          renderApps();
        });
      });
    };
    const fetchWeather = async () => {
      $("weatherBtn").disabled = true;
      $("weatherHint").textContent = "获取天气中...";
      try {
        let payload = { city: $("cityInput").value.trim() || "Nanchang Honggutan" };
        if (navigator.geolocation) {
          try {
            const pos = await new Promise((resolve, reject) => navigator.geolocation.getCurrentPosition(resolve, reject, { timeout: 6000 }));
            payload = { latitude: pos.coords.latitude, longitude: pos.coords.longitude, city: payload.city };
          } catch (err) {
            log(`定位不可用，使用城市: ${payload.city}`);
          }
        }
        const data = await api("/api/weather/resolve", { method: "POST", body: JSON.stringify(payload) });
        state.weather = data.weather;
        $("weatherText").value = conditionLabel(state.weather);
        $("weatherHint").textContent = "天气已准备好，安装 Weather Pet 时会一起打包。";
        log(`weather ${conditionLabel(state.weather)} code=${state.weather.weather_code}`);
      } catch (err) {
        state.weather = null;
        $("weatherText").value = "尚未获取";
        $("weatherHint").textContent = friendlyWeatherError(err.message);
        log(`weather failed: ${err.message}`);
      } finally {
        $("weatherBtn").disabled = false;
      }
    };
    const renderApps = () => {
      renderFilters();
      const visible = state.storeCategory === "All"
        ? state.apps
        : state.apps.filter(app => (app.category || "General") === state.storeCategory);
      $("apps").innerHTML = visible.map(app => `
        <article class="app" style="--accent:${esc(app.accent)}">
          <div class="preview">${preview(app)}</div>
          <div class="app-body">
            <div class="meta"><span class="badge">${esc(displayCategory(app.category || "General"))}</span><span>${app.requiresWeather ? "需要天气" : "离线可装"}</span></div>
            <h3>${esc(app.name)}</h3>
            <div class="byline"><span>${esc(app.author || "Huangshan Runtime")}</span><span>${esc(app.id)}</span></div>
            <p>${esc(app.summary || app.description || "")}</p>
            <div class="reqs">${requirements(app).map(item => `<span class="req">${esc(item)}</span>`).join("")}</div>
            <div class="app-actions">
              <button data-install="${esc(app.id)}" ${state.installing ? "disabled" : ""}>安装到黄山派</button>
              <button class="export" data-export="${esc(app.id)}">导出</button>
            </div>
          </div>
        </article>
      `).join("");
      document.querySelectorAll("[data-install]").forEach(button => {
        button.addEventListener("click", () => installApp(button.dataset.install));
      });
      document.querySelectorAll("[data-export]").forEach(button => {
        button.addEventListener("click", () => {
          const appId = button.dataset.export;
          log(`export ${appId}`);
          window.location.href = `/api/apps/${encodeURIComponent(appId)}/export`;
        });
      });
    };
    const renderImportResult = () => {
      const target = $("importResult");
      const item = state.importedPackage;
      if (!item) {
        target.className = "hint";
        target.innerHTML = "尚未导入包。";
        return;
      }
      const pkg = item.package || {};
      target.className = "import-result";
      target.innerHTML = `
        <div class="kv">
          <div><b>${esc(pkg.name || pkg.id)}</b> · ${esc(pkg.id)}</div>
          <div>${esc(pkg.description || "Runtime app package")}</div>
          <div>${requirements(pkg).map(value => `<span class="req">${esc(value)}</span>`).join("")}</div>
          <div>${esc(pkg.fileCount || 0)} files · ${esc(pkg.byteCount || 0)} bytes · screenshot ${esc(pkg.screenshotCheck || pkg.screenshot || "--")}</div>
        </div>
        <div class="runtime-actions" style="margin-top:12px">
          <button id="installImported" class="secondary">安装导入包</button>
        </div>
      `;
      $("installImported").addEventListener("click", installImportedPackage);
    };
    const samplePlan = () => ({
      app: {
        id: "plan_demo",
        name: "Plan Demo",
        description: "Generated locally from a Huangshan Runtime app plan.",
        category: "Tools",
        icon: "sparkles",
        author: "Runtime App Plan Writer",
        requirements: ["Runtime", "Display"],
        capabilities: ["display"]
      },
      components: [
        { type: "label", text: "Generated safely", color: "#5eead4" }
      ]
    });
    const fillPlanSample = () => {
      $("planText").value = JSON.stringify(samplePlan(), null, 2);
      $("planResult").textContent = "示例已填入，可直接生成。";
    };
    const generatePlanPackage = async () => {
      const raw = $("planText").value.trim();
      if (!raw) {
        $("planResult").className = "runtime-note warn";
        $("planResult").textContent = "请先粘贴 JSON plan。";
        return;
      }
      $("planGenerate").disabled = true;
      $("planResult").className = "hint";
      $("planResult").textContent = "正在生成并校验 plan...";
      try {
        const plan = JSON.parse(raw);
        const data = await api("/api/apps/plan", {
          method: "POST",
          body: JSON.stringify({ plan })
        });
        state.importedPackage = data;
        renderImportResult();
        $("planResult").className = "import-result";
        $("planResult").innerHTML = `<div class="kv"><div><b>${esc(data.package?.name || data.package?.id)}</b> · ${esc(data.package?.id)}</div><div>${esc(data.package?.fileCount || 0)} files · ${esc(data.package?.byteCount || 0)} bytes · 可安装</div></div>`;
        log(`plan package ok ${data.package?.id || ""}`);
      } catch (err) {
        $("planResult").className = "runtime-note warn";
        $("planResult").textContent = err.message;
        log(`plan package failed: ${err.message}`);
      } finally {
        $("planGenerate").disabled = false;
      }
    };
    const importPackageFile = async file => {
      if (!file) return;
      $("importResult").className = "hint";
      $("importResult").textContent = `校验 ${file.name}...`;
      try {
        const data = await apiUpload("/api/apps/import", file);
        state.importedPackage = data;
        renderImportResult();
        log(`import ok ${data.package?.id || ""}`);
      } catch (err) {
        state.importedPackage = null;
        $("importResult").className = "runtime-note warn";
        $("importResult").textContent = err.message;
        log(`import failed: ${err.message}`);
      }
    };
    const installImportedPackage = async () => {
      if (!state.importedPackage?.token) return;
      state.installing = true;
      setInstallProgress(3);
      $("jobText").textContent = "准备安装导入包";
      try {
        const data = await api("/api/apps/import/install", {
          method: "POST",
          body: JSON.stringify({ token: state.importedPackage.token })
        });
        const job = await pollJob(data.job_id);
        if (job?.status === "done") await refreshRuntimeApps();
      } catch (err) {
        log(`import install failed: ${err.message}`);
        clearInstallProgress();
        $("jobText").textContent = err.message;
      } finally {
        state.installing = false;
        renderApps();
      }
    };
    const renderRuntimeApps = () => {
      const activeNote = state.runtimeActive
        ? state.runtimeState === "running"
          ? ` · 运行 ${esc(state.runtimeActive)}`
          : ` · 已选 ${esc(state.runtimeActive)}`
        : "";
      const note = state.runtimeLoading
        ? `<div class="runtime-note">正在通过 BLE 读取板上 App 列表...</div>`
        : state.runtimeError
          ? `<div class="runtime-note warn">${esc(state.runtimeError)}${state.runtimeApps.length ? "，下面保留上一次成功读取的结果。" : "。"}</div>`
          : state.runtimeLastUpdated
            ? `<div class="runtime-note">上次刷新 ${esc(state.runtimeLastUpdated)}${activeNote}</div>`
            : "";
      if (!state.runtimeApps.length) {
        $("runtimeApps").innerHTML = note || `<div class="hint">尚未读取到板上 App。</div>`;
        return;
      }
      $("runtimeApps").innerHTML = note + state.runtimeApps.map(app => {
        const display = runtimeDisplayApp(app);
        return `
        <div class="runtime-card ${app.active ? "active" : ""}" style="--accent:${esc(display.accent)}">
          <div class="runtime-shot">${preview(display)}</div>
          <div>
            <div class="meta"><span class="badge">${esc(displayCategory(display.category))}</span><span>${esc(display.author)}</span></div>
            <div class="runtime-name">${app.active ? "* " : ""}${esc(app.name || app.id)}</div>
            <div class="runtime-desc">${esc(display.description)}</div>
            <div class="reqs">${requirements(display).map(item => `<span class="req">${esc(item)}</span>`).join("")}</div>
            <div class="runtime-state">${esc(app.id)} · ${app.compatible ? "兼容" : "不兼容"} · ${app.manifest ? "manifest" : "app.info"}</div>
          </div>
          <div class="runtime-actions">
            <button data-runtime-launch="${esc(app.id)}" ${state.runtimeBusy || state.runtimeLoading ? "disabled" : ""}>启动</button>
            <button data-runtime-delete="${esc(app.id)}" class="danger" ${state.runtimeBusy || state.runtimeLoading || app.active ? "disabled" : ""}>删除</button>
          </div>
        </div>
      `;
      }).join("");
      document.querySelectorAll("[data-runtime-launch]").forEach(button => {
        button.addEventListener("click", () => runtimeAction("launch", button.dataset.runtimeLaunch));
      });
      document.querySelectorAll("[data-runtime-delete]").forEach(button => {
        button.addEventListener("click", () => runtimeAction("delete", button.dataset.runtimeDelete));
      });
    };
    const refreshRuntimeApps = async () => {
      state.runtimeLoading = true;
      state.runtimeError = "";
      renderRuntimeApps();
      try {
        const data = await api("/api/runtime/apps");
        if (!data.ok) throw new Error(data.error || "Runtime App Manager unavailable");
        state.runtimeApps = runtimeAppsPayload(data);
        state.runtimeActive = runtimeActive(data);
        state.runtimeState = runtimeState(data);
        state.runtimeLastUpdated = new Date().toLocaleTimeString();
        state.runtimeError = "";
        setTransport(true, runtimeConnectionSummary(state.runtimeState));
        renderRuntimeApps();
        log(`runtime apps ${state.runtimeApps.length} active=${state.runtimeActive}`);
      } catch (err) {
        state.runtimeError = friendlyRuntimeAppsError(err.message);
        renderRuntimeApps();
        log(`runtime apps failed: ${state.runtimeError}`);
      } finally {
        state.runtimeLoading = false;
        renderRuntimeApps();
      }
    };
    const renderDiagnostics = () => {
      const data = state.diagnostics;
      if (!data) {
        $("diagnosticSummary").innerHTML = `<div class="runtime-note">等待检测。</div>`;
        return;
      }
      const checks = data.checks || {};
      const labels = { status: "Runtime", capabilities: "能力握手", appStatus: "App 状态", installedApps: "已装 App", ble: "BLE" };
      $("diagnosticSummary").innerHTML = Object.keys(labels).map(key => {
        const item = checks[key] || {};
        return `<div class="check ${item.ok ? "ok" : "bad"}">
          <div class="check-title">${esc(labels[key])}</div>
          <div class="check-body">${item.ok ? "ok" : esc(item.error || "failed")}</div>
        </div>`;
      }).join("");
      const facts = data.runtime?.facts || {};
      const ports = data.serial?.ports || [];
      $("recoveryRuntime").innerHTML = `
        <div><b>Transport</b> ${esc(data.transport)} ${data.serial?.port ? `· ${esc(data.serial.port)}` : ""}</div>
        <div><b>Serial ports</b> ${ports.length ? ports.map(p => `${esc(p.path)}${p.selected ? " *" : ""}`).join("<br>") : "未检测到"}</div>
        <div><b>SD card</b> ${esc(data.sdCard?.state || "unknown")}</div>
        <div><b>Active</b> ${esc(facts.active || data.runtime?.appStatus?.active || "--")}</div>
        <div><b>State</b> ${esc(facts.state || data.runtime?.appStatus?.state || "--")}</div>
      `;
      const apps = data.apps?.apps || [];
      $("recoveryApps").innerHTML = apps.length
        ? apps.map(app => `<div><b>${esc(app.name || app.id)}</b> · ${esc(app.id)} ${app.active ? "· active" : ""}</div>`).join("")
        : `<div>未读取到已安装 App。</div>`;
      const errors = data.recentErrors || [];
      $("recoveryErrors").innerHTML = errors.length
        ? errors.map(item => `<div>${esc(item)}</div>`).join("")
        : `<div>暂无 Runtime 最近错误。</div>`;
      $("recoveryHint").textContent = data.transport === "serial"
        ? "串口模式可执行完整 Runtime 恢复；BLE 模式可清 staging 和重装示例 App。"
        : "BLE 模式可清 staging 和重装示例 App；恢复 Runtime 需要用串口模式启动 App Store。";
    };
    const refreshDiagnostics = async () => {
      $("diagnosticSummary").innerHTML = `<div class="runtime-note">正在检测 Runtime、SD 卡、BLE 和 App 列表...</div>`;
      try {
        state.diagnostics = await api("/api/recovery/diagnostics");
        renderDiagnostics();
        log(`diagnostics ok ${state.diagnostics.durationMs || 0}ms`);
      } catch (err) {
        $("diagnosticSummary").innerHTML = `<div class="runtime-note warn">${esc(err.message)}</div>`;
        log(`diagnostics failed: ${err.message}`);
      }
    };
    const startRecoveryJob = async (url, body = {}) => {
      const data = await api(url, { method: "POST", body: JSON.stringify(body) });
      const job = await pollJob(data.job_id);
      if (job?.status === "done") {
        await refreshDiagnostics();
        await refreshRuntimeApps();
      }
    };
    const renderCapabilityMatrix = () => {
      const rows = state.capabilityMatrix || [];
      $("capabilityMatrix").innerHTML = rows.length ? rows.map(row => `
        <div class="cap-row">
          <div class="name">${esc(row.name)}</div>
          <div><span class="state-pill ${row.available ? "ok" : "bad"}">${row.available ? "可用" : "不可用"}</span></div>
          <div>${esc(row.apiVersion || row.apiName || "--")}</div>
          <div>${(row.examples || []).map(id => `<span class="req">${esc(id)}</span>`).join("")}</div>
          <div>${(row.manifest || []).slice(0, 3).map(cap => `<span class="req">${esc(cap)}</span>`).join("")}</div>
        </div>
      `).join("") : `<div class="runtime-note">等待能力握手。</div>`;
    };
    const refreshCapabilityMatrix = async () => {
      $("capabilityMatrix").innerHTML = `<div class="runtime-note">正在读取 Runtime capabilities...</div>`;
      try {
        const data = await api("/api/capabilities/matrix");
        state.capabilityMatrix = data.matrix || [];
        renderCapabilityMatrix();
        log(`capability matrix ${state.capabilityMatrix.length}`);
      } catch (err) {
        const text = friendlyCapabilityError(err.message);
        $("capabilityMatrix").innerHTML = `<div class="runtime-note warn">${esc(text)}</div>`;
        log(`capability matrix failed: ${text}`);
      }
    };
    const runtimeAction = async (action, appId) => {
      state.runtimeBusy = true;
      renderRuntimeApps();
      try {
        const path = action === "stop" ? "/api/runtime/apps/stop" : `/api/runtime/apps/${encodeURIComponent(appId)}/${action}`;
        const data = await api(path, { method: "POST", body: "{}" });
        log(`${action} ${appId || "active"}: ${statusLine(data.status)}`);
        const now = new Date().toLocaleTimeString();
        if (action === "launch" && appId) {
          state.runtimeActive = appId;
          state.runtimeState = "running";
          state.runtimeApps = state.runtimeApps.map(app => ({ ...app, active: app.id === appId ? 1 : 0 }));
          state.runtimeLastUpdated = `${now} · 已发送启动命令`;
          setTransport(true, runtimeConnectionSummary(state.runtimeState));
        } else if (action === "stop") {
          state.runtimeActive = "welcome";
          state.runtimeState = "idle";
          state.runtimeApps = state.runtimeApps.map(app => ({ ...app, active: 0 }));
          state.runtimeLastUpdated = `${now} · 已发送停止命令`;
          setTransport(true, runtimeConnectionSummary(state.runtimeState));
        } else if (action === "delete" && appId) {
          state.runtimeApps = state.runtimeApps.filter(app => app.id !== appId);
          state.runtimeLastUpdated = `${now} · 已发送删除命令`;
        }
        state.runtimeError = "";
        // Do not immediately query status/apps after a launch. Serial reads during LVGL transitions
        // can disturb the watch UI and make the screen flicker or bounce back to Home.
      } catch (err) {
        log(`${action} failed: ${err.message}`);
      } finally {
        state.runtimeBusy = false;
        renderRuntimeApps();
      }
    };
    const pollJob = async jobId => {
      let finalJob = null;
      let done = false;
      while (!done) {
        await new Promise(r => setTimeout(r, 700));
        const job = await api(`/api/jobs/${jobId}`);
        finalJob = job;
        setInstallProgress(job.progress || 0);
        const visibleLog = job.log.map(line => compactText(line, 260)).join("\n");
        $("jobText").textContent = installChunkText(job.message || job.status, visibleLog);
        $("log").textContent = visibleLog + "\n";
        $("log").scrollTop = $("log").scrollHeight;
        done = job.status === "done" || job.status === "failed";
        if (done) {
          clearInstallProgress();
          if (job.status === "done") {
            $("jobText").textContent = `已安装 ${displayAppName(job.app_id)}`;
            log(`install complete: ${job.app_id}`);
          } else {
            $("jobText").textContent = `安装失败：${compactText(job.message, 120)}`;
            log(`install failed: ${job.message}`);
          }
        }
      }
      return finalJob;
    };
    const installApp = async appId => {
      const app = state.apps.find(item => item.id === appId);
      if (app?.requiresWeather && !state.weather) {
        await fetchWeather();
        if (!state.weather) return;
      }
      state.installing = true;
      renderApps();
      setInstallProgress(3);
      $("jobText").textContent = `准备安装 ${appId}`;
      try {
        const requestId = `install-${Date.now()}-${Math.random().toString(16).slice(2)}`;
        const data = await createInstallJob(appId, { weather: state.weather, request_id: requestId });
        const job = await pollJob(data.job_id);
        if (!job || job.status !== "done") return;
        if (!state.runtimeApps.some(item => item.id === appId)) {
          state.runtimeApps = [...state.runtimeApps, {
            id: appId,
            name: app?.name || appId,
            description: app?.description || app?.summary || "",
            category: app?.category || "General",
            icon: app?.icon || "app",
            iconText: app?.iconText || iconText(app),
            author: app?.author || "Huangshan Runtime",
            screenshot: app?.screenshot || `generated:${appId}`,
            requirements: app?.requirements || [],
            active: 0,
            compatible: 1,
            manifest: 1,
            app_info: 0,
            main_lua: 1
          }].sort((a, b) => String(a.name || a.id).localeCompare(String(b.name || b.id)));
        }
        state.runtimeActive = appId;
        state.runtimeState = "";
        state.runtimeApps = state.runtimeApps.map(item => ({ ...item, active: item.id === appId ? 1 : 0 }));
        state.runtimeLastUpdated = `${new Date().toLocaleTimeString()} · 已发送安装命令`;
        state.runtimeError = "";
        setTransport(true, runtimeConnectionSummary(state.runtimeState));
        renderRuntimeApps();
      } catch (err) {
        log(`install request failed: ${err.message}`);
        clearInstallProgress();
        $("jobText").textContent = err.message;
      } finally {
        state.installing = false;
        renderApps();
      }
    };
    const showPage = page => {
      state.currentPage = page;
      document.querySelectorAll("[data-page]").forEach(button => {
        button.classList.toggle("active", button.dataset.page === page);
      });
      document.querySelectorAll(".page").forEach(panel => {
        panel.classList.toggle("active", panel.id === `page-${page}`);
      });
      if (page === "recovery" && !state.diagnostics) refreshDiagnostics();
      if (page === "capabilities" && !state.capabilityMatrix.length) refreshCapabilityMatrix();
    };
    const bindPackageDrop = () => {
      const dropzone = $("dropzone");
      const input = $("packageFile");
      input.addEventListener("change", () => importPackageFile(input.files && input.files[0]));
      ["dragenter", "dragover"].forEach(type => {
        dropzone.addEventListener(type, event => {
          event.preventDefault();
          dropzone.classList.add("drag");
        });
      });
      ["dragleave", "drop"].forEach(type => {
        dropzone.addEventListener(type, event => {
          event.preventDefault();
          dropzone.classList.remove("drag");
        });
      });
      dropzone.addEventListener("drop", event => {
        const file = event.dataTransfer?.files?.[0];
        importPackageFile(file);
      });
    };
    const boot = async () => {
      const apps = await api("/api/apps");
      state.apps = apps.apps;
      renderApps();
      renderImportResult();
      await refreshTransport();
      await refreshCapabilities();
      await refreshRuntimeApps();
      fetchWeather();
      // Keep serial transport quiet after initial load; periodic status polling can disturb the watch UI.
      document.querySelectorAll("[data-page]").forEach(button => {
        button.addEventListener("click", () => showPage(button.dataset.page));
      });
      bindPackageDrop();
      fillPlanSample();
      $("planSample").addEventListener("click", fillPlanSample);
      $("planGenerate").addEventListener("click", generatePlanPackage);
      $("weatherBtn").addEventListener("click", fetchWeather);
      $("runtimeRefresh").addEventListener("click", refreshRuntimeApps);
      $("runtimeStop").addEventListener("click", () => runtimeAction("stop"));
      $("recoveryRefresh").addEventListener("click", refreshDiagnostics);
      $("capabilityRefresh").addEventListener("click", refreshCapabilityMatrix);
      $("clearStaging").addEventListener("click", () => startRecoveryJob("/api/recovery/staging-clear"));
      $("reinstallExamples").addEventListener("click", () => startRecoveryJob("/api/recovery/reinstall-examples"));
      $("flashRuntime").addEventListener("click", () => {
        if (!confirm("确认通过串口重新烧录 Runtime？这会重启设备。")) return;
        const port = state.diagnostics?.serial?.port || "";
        startRecoveryJob("/api/recovery/flash-runtime", { confirm: "FLASH_RUNTIME", port });
      });
      const initialPage = location.pathname === "/capabilities"
        ? "capabilities"
        : location.pathname === "/recovery"
          ? "recovery"
          : "store";
      showPage(initialPage);
    };
    boot().catch(err => log(`boot failed: ${err.message}`));
  </script>
</body>
</html>
"""


def json_bytes(data: Any) -> bytes:
    return json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def normalize_requirements(value: Any) -> list[str]:
    if isinstance(value, list):
        return [str(item).strip() for item in value if str(item).strip()]
    if isinstance(value, str):
        return [item.strip() for item in value.split(",") if item.strip()]
    return []


def app_screenshot_url(app_id: str, screenshot: str) -> str:
    if not screenshot:
        return f"/api/apps/{urllib.parse.quote(app_id)}/generated-screenshot.png"
    if screenshot.startswith("generated:"):
        return f"/api/apps/{urllib.parse.quote(app_id)}/generated-screenshot.png"
    try:
        safe = safe_package_path(screenshot)
    except RuntimePackageError:
        return f"generated:{app_id}"
    return f"/api/apps/{urllib.parse.quote(app_id)}/asset/{urllib.parse.quote(safe)}"


def app_entry(app_id: str) -> dict[str, Any]:
    app_dir = RUNTIME_APPS_DIR / app_id
    manifest = read_json(app_dir / "manifest.json")
    meta = APP_META.get(app_id, {})
    category = str(manifest.get("category") or meta.get("category") or "General")
    icon = str(manifest.get("icon") or meta.get("icon") or "app")
    author = str(manifest.get("author") or meta.get("author") or "Huangshan Runtime")
    screenshot = str(manifest.get("screenshot") or meta.get("screenshot") or f"generated:{app_id}")
    requirements = normalize_requirements(manifest.get("requirements") or meta.get("requirements") or [])
    return {
        "id": app_id,
        "name": manifest.get("name", app_id),
        "description": manifest.get("description", ""),
        "summary": meta.get("summary") or manifest.get("description", ""),
        "category": category,
        "icon": icon,
        "iconText": ICON_GLYPHS.get(icon, icon[:1].upper() if icon else "A"),
        "author": author,
        "screenshot": app_screenshot_url(app_id, screenshot),
        "requirements": requirements,
        "accent": meta.get("accent", "#5db7ff"),
        "requiresWeather": bool(meta.get("requiresWeather")),
    }


def list_apps() -> list[dict[str, Any]]:
    apps = []
    for app_id in APP_ORDER:
        if (RUNTIME_APPS_DIR / app_id / "manifest.json").exists():
            apps.append(app_entry(app_id))
    return apps


def normalize_runtime_app_screenshots(payload: dict[str, Any]) -> dict[str, Any]:
    apps = payload.get("apps")
    if not isinstance(apps, list):
        return payload
    for app in apps:
        if not isinstance(app, dict):
            continue
        app_id = str(app.get("id") or "")
        if app_id in APP_ORDER and (RUNTIME_APPS_DIR / app_id / "manifest.json").is_file():
            app["screenshot"] = app_entry(app_id)["screenshot"]
            continue
        screenshot = str(app.get("screenshot") or "")
        if not screenshot.startswith("/api/apps/"):
            app["screenshot"] = f"generated:{app_id or 'app'}"
    return payload


def local_app_asset(app_id: str, raw_path: str) -> tuple[bytes, str]:
    app_id = safe_package_id(app_id)
    rel = safe_package_path(urllib.parse.unquote(raw_path))
    path = (RUNTIME_APPS_DIR / app_id / rel).resolve()
    app_root = (RUNTIME_APPS_DIR / app_id).resolve()
    if app_root not in path.parents:
        raise ValueError("asset path escapes app directory")
    if not path.is_file():
        raise FileNotFoundError(rel)
    content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
    return path.read_bytes(), content_type


def package_metadata(package_id: str, files: dict[str, bytes]) -> dict[str, Any]:
    manifest: dict[str, Any] = {}
    if "manifest.json" in files:
        manifest = json.loads(files["manifest.json"].decode("utf-8"))
    description = str(manifest.get("description") or "")
    icon = str(manifest.get("icon") or "app")
    screenshot = str(manifest.get("screenshot") or f"generated:{package_id}")
    return {
        "id": package_id,
        "name": str(manifest.get("name") or package_id),
        "description": description,
        "summary": description,
        "category": str(manifest.get("category") or "Imported"),
        "icon": icon,
        "iconText": ICON_GLYPHS.get(icon, icon[:1].upper() if icon else "A"),
        "author": str(manifest.get("author") or "Imported Package"),
        "screenshot": screenshot,
        "requirements": normalize_requirements(manifest.get("requirements") or []),
        "fileCount": len(files),
        "byteCount": sum(len(data) for data in files.values()),
    }


def validate_screenshot_reference(package_id: str, files: dict[str, bytes]) -> str:
    if "manifest.json" not in files:
        return "no manifest screenshot"
    manifest = json.loads(files["manifest.json"].decode("utf-8"))
    screenshot = str(manifest.get("screenshot") or "")
    if not screenshot or screenshot.startswith("generated:"):
        return screenshot or "generated"
    safe = safe_package_path(screenshot)
    if safe not in files:
        raise RuntimeError(f"manifest screenshot {safe!r} is missing from package")
    if not files[safe].startswith((b"\x89PNG\r\n\x1a\n", b"\xff\xd8")):
        raise RuntimeError(f"manifest screenshot {safe!r} is not a PNG/JPEG file")
    return safe


def normalize_zip_member(name: str) -> str:
    raw = name.replace("\\", "/").strip("/")
    parts = [part for part in raw.split("/") if part and part != "."]
    if not parts or any(part == ".." for part in parts):
        raise RuntimeError(f"unsafe zip member path: {name!r}")
    if len(parts) > 1 and parts[0] not in {"assets", "images", "fonts", "lib"}:
        raw = "/".join(parts[1:])
    return safe_package_path(raw)


def package_from_archive(blob: bytes) -> tuple[str, dict[str, bytes], str]:
    if len(blob) > IMPORT_MAX_BYTES:
        raise RuntimeError(f"package is too large; max {IMPORT_MAX_BYTES // (1024 * 1024)} MB")
    files: dict[str, bytes] = {}
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            rel = normalize_zip_member(info.filename)
            if rel in files:
                raise RuntimeError(f"duplicate package path after normalization: {rel}")
            if info.file_size > IMPORT_MAX_BYTES:
                raise RuntimeError(f"package member too large: {rel}")
            files[rel] = archive.read(info)
    if "manifest.json" in files:
        manifest = json.loads(files["manifest.json"].decode("utf-8"))
        app_id = safe_package_id(str(manifest.get("id") or ""))
    else:
        raise RuntimeError("imported package must include manifest.json")
    with package_errors_as_exceptions():
        package_id, safe_files = validate_package(app_id, files)
    screenshot = validate_screenshot_reference(package_id, safe_files)
    return package_id, safe_files, screenshot


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def generated_screenshot_png(seed: str, icon_text: str, accent: str = "#5db7ff") -> bytes:
    width = 320
    height = 220
    try:
        accent_rgb = (int(accent[1:3], 16), int(accent[3:5], 16), int(accent[5:7], 16))
    except Exception:
        accent_rgb = (93, 183, 255)
    dark = (13, 20, 26)
    panel = (20, 31, 41)
    white = (238, 244, 248)
    muted = (148, 163, 184)

    def mix(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
        return tuple(int(a[index] + (b[index] - a[index]) * t) for index in range(3))

    pixels = bytearray(width * height * 3)

    def set_px(x: int, y: int, color: tuple[int, int, int]) -> None:
        if 0 <= x < width and 0 <= y < height:
            offset = (y * width + x) * 3
            pixels[offset:offset + 3] = bytes(color)

    def rect(x0: int, y0: int, x1: int, y1: int, color: tuple[int, int, int]) -> None:
        x0 = max(0, x0)
        y0 = max(0, y0)
        x1 = min(width, x1)
        y1 = min(height, y1)
        for yy in range(y0, y1):
            offset = (yy * width + x0) * 3
            pixels[offset:offset + (x1 - x0) * 3] = bytes(color) * max(0, x1 - x0)

    def circle(cx: int, cy: int, radius: int, color: tuple[int, int, int]) -> None:
        radius2 = radius * radius
        for yy in range(cy - radius, cy + radius + 1):
            for xx in range(cx - radius, cx + radius + 1):
                if (xx - cx) * (xx - cx) + (yy - cy) * (yy - cy) <= radius2:
                    set_px(xx, yy, color)

    def seven_segment_digit(x: int, y: int, digit: int, color: tuple[int, int, int]) -> None:
        segments = {
            0: "abcedf",
            1: "bc",
            2: "abdeg",
            3: "abcdg",
            4: "bcfg",
            5: "acdfg",
            6: "acdefg",
            7: "abc",
            8: "abcdefg",
            9: "abcdfg",
        }[digit]
        geometry = {
            "a": (x + 4, y, x + 18, y + 4),
            "b": (x + 18, y + 4, x + 22, y + 20),
            "c": (x + 18, y + 24, x + 22, y + 40),
            "d": (x + 4, y + 40, x + 18, y + 44),
            "e": (x, y + 24, x + 4, y + 40),
            "f": (x, y + 4, x + 4, y + 20),
            "g": (x + 4, y + 20, x + 18, y + 24),
        }
        for segment in segments:
            rect(*geometry[segment], color)

    for y in range(height):
        for x in range(width):
            t = (x * 0.65 + y) / (width * 0.65 + height)
            color = mix(dark, accent_rgb, t * 0.58)
            set_px(x, y, color)

    rect(32, 24, 288, 196, mix(panel, accent_rgb, 0.08))
    rect(44, 36, 276, 184, mix(panel, accent_rgb, 0.12))

    app_key = seed.lower()
    if "thunder" in app_key:
        space = (5, 11, 24)
        cyan = (56, 189, 248)
        mint = (94, 234, 212)
        red = (255, 64, 88)
        amber = (251, 191, 36)
        rect(48, 28, 272, 196, space)
        for index in range(18):
            x = 56 + (index * 47) % 208
            y = 34 + (index * 31) % 152
            circle(x, y, 1 + (index % 5 == 0), muted if index & 1 else white)
        rect(145, 157, 175, 164, cyan)
        rect(154, 141, 166, 184, mint)
        rect(157, 137, 163, 146, white)
        rect(158, 184, 162, 193, amber)
        for x, y in ((84, 62), (142, 78), (218, 54), (190, 104)):
            rect(x, y, x + 28, y + 16, red)
            rect(x + 8, y - 5, x + 20, y + 20, (255, 107, 117))
        rect(123, 38, 197, 48, red)
        rect(135, 32, 185, 54, (190, 24, 52))
        for x in (155, 159, 163):
            rect(x, 118, x + 2, 137, mint)
        for x, y in ((96, 91), (226, 86), (180, 122)):
            circle(x, y, 3, red)
    elif "imu" in app_key:
        dial_bg = (12, 28, 43)
        grid = (53, 81, 111)
        circle(160, 105, 78, accent_rgb)
        circle(160, 105, 74, dial_bg)
        circle(160, 105, 52, grid)
        circle(160, 105, 50, dial_bg)
        circle(160, 105, 28, grid)
        circle(160, 105, 26, dial_bg)
        rect(88, 104, 232, 106, grid)
        rect(159, 33, 161, 177, grid)
        circle(160, 105, 11, (71, 215, 172))
        circle(160, 105, 8, dial_bg)
        circle(193, 77, 13, (255, 181, 191))
        circle(193, 77, 10, (255, 64, 88))
        rect(84, 188, 145, 192, (226, 232, 240))
        rect(175, 188, 236, 192, (226, 232, 240))
    elif "breakout" in app_key:
        colors = [(255, 92, 101), (251, 191, 36), (71, 215, 172), (93, 183, 255)]
        for row in range(4):
            for col in range(6):
                x0 = 57 + col * 35
                y0 = 48 + row * 22
                rect(x0, y0, x0 + 29, y0 + 14, colors[row])
        rect(112, 174, 208, 184, (71, 215, 172))
        circle(178, 150, 7, white)
        rect(66, 38, 254, 40, (38, 59, 85))
    elif "pomodoro" in app_key:
        tomato = (255, 92, 101)
        tomato_dark = (180, 48, 65)
        leaf = (71, 215, 172)
        clock_bg = (12, 18, 24)
        circle(160, 105, 72, tomato_dark)
        circle(160, 105, 65, tomato)
        circle(160, 105, 51, clock_bg)
        circle(151, 35, 12, leaf)
        circle(168, 35, 12, leaf)
        rect(157, 20, 164, 43, leaf)
        seven_segment_digit(104, 82, 2, white)
        seven_segment_digit(130, 82, 5, white)
        circle(157, 96, 3, leaf)
        circle(157, 112, 3, leaf)
        seven_segment_digit(166, 82, 0, white)
        seven_segment_digit(192, 82, 0, white)
        rect(105, 170, 151, 182, tomato)
        rect(157, 170, 203, 182, (39, 50, 68))
        rect(209, 170, 255, 182, (39, 50, 68))
    elif "2048" in app_key:
        board = [
            [(238, 228, 218), (237, 224, 200), (242, 177, 121), (245, 149, 99)],
            [(246, 124, 95), (246, 94, 59), (237, 207, 114), (237, 204, 97)],
            [(237, 200, 80), (237, 197, 63), (238, 228, 218), (187, 173, 160)],
            [(205, 193, 180), (238, 228, 218), (242, 177, 121), (237, 204, 97)],
        ]
        rect(84, 34, 236, 186, (94, 69, 50))
        size = 30
        gap = 7
        for row in range(4):
            for col in range(4):
                x0 = 94 + col * (size + gap)
                y0 = 44 + row * (size + gap)
                rect(x0, y0, x0 + size, y0 + size, board[row][col])
        rect(118, 84, 202, 96, (255, 255, 255))
        rect(118, 126, 202, 138, (255, 255, 255))
    elif "snake" in app_key:
        grid = (17, 58, 42)
        rect(66, 38, 254, 182, grid)
        for x in range(66, 255, 17):
            rect(x, 38, x + 1, 182, (24, 86, 58))
        for y in range(38, 183, 17):
            rect(66, y, 254, y + 1, (24, 86, 58))
        snake = [(100, 123), (117, 123), (134, 123), (151, 123), (151, 106), (168, 106), (185, 106)]
        for index, (x, y) in enumerate(snake):
            rect(x, y, x + 15, y + 15, (34, 197, 94) if index < len(snake) - 1 else (134, 239, 172))
        circle(219, 72, 8, (251, 113, 133))
    elif "sensor" in app_key:
        rect(64, 52, 256, 164, (15, 34, 46))
        bars = [42, 74, 55, 98, 66]
        colors = [(56, 189, 248), (45, 212, 191), (251, 191, 36), (129, 140, 248), (248, 113, 113)]
        for index, value in enumerate(bars):
            x0 = 84 + index * 32
            rect(x0, 150 - value, x0 + 18, 150, colors[index])
        for x in range(82, 230, 30):
            circle(x, 174, 4, muted)
        rect(82, 172, 230, 176, muted)
    elif "weather" in app_key:
        circle(138, 94, 32, (255, 255, 255))
        circle(174, 96, 42, (226, 241, 255))
        circle(204, 108, 28, (255, 255, 255))
        rect(111, 108, 226, 134, (236, 248, 255))
        circle(212, 64, 22, (251, 191, 36))
    elif "touch" in app_key:
        rect(96, 36, 224, 184, (17, 24, 39))
        circle(160, 112, 38, mix(accent_rgb, white, 0.25))
        circle(160, 112, 13, white)
        rect(116, 150, 204, 156, (56, 189, 248))
    elif "voice" in app_key:
        rect(70, 48, 250, 168, (15, 23, 42))
        for index, value in enumerate([24, 52, 84, 44, 70, 34, 96, 58, 28]):
            x0 = 92 + index * 16
            rect(x0, 112 - value // 2, x0 + 8, 112 + value // 2, mix(accent_rgb, white, 0.2))
        circle(160, 176, 9, (251, 113, 133))
    elif "power" in app_key:
        rect(88, 72, 226, 132, (15, 23, 42))
        rect(226, 92, 238, 112, (15, 23, 42))
        rect(98, 82, 178, 122, (53, 213, 159))
        rect(184, 82, 214, 122, (251, 191, 36))
    elif "rgb" in app_key:
        circle(130, 96, 34, (239, 68, 68))
        circle(170, 96, 34, (34, 197, 94))
        circle(150, 134, 34, (59, 130, 246))
    elif "jump" in app_key:
        night = (5, 11, 20)
        mint = (71, 215, 172)
        amber = (251, 191, 36)
        blue = (93, 183, 255)
        rect(48, 30, 272, 196, night)
        for index in range(8):
            circle(60 + (index * 29) % 200, 42 + (index * 23) % 60, 1, muted if index & 1 else white)
        rect(196, 82, 240, 100, mix(amber, night, 0.55))
        rect(200, 86, 236, 96, amber)
        for step in range(11):
            u = step / 10.0
            dot_x = int(118 + (206 - 118) * u)
            dot_y = int(128 + (74 - 128) * u - 22 * 4 * u * (1 - u))
            circle(dot_x, dot_y, 1, blue)
        rect(82, 148, 140, 168, mix(mint, night, 0.55))
        rect(86, 152, 136, 164, mint)
        circle(110, 140, 10, mix(blue, night, 0.45))
        circle(110, 140, 8, white)
        rect(60, 182, 260, 190, (30, 41, 59))
        rect(60, 182, 198, 190, amber)
    else:
        rect(72, 54, 248, 82, mix(accent_rgb, white, 0.15))
        rect(72, 96, 208, 114, mix(accent_rgb, white, 0.35))
        rect(72, 128, 232, 146, mix(accent_rgb, white, 0.22))
        rect(72, 160, 172, 174, mix(accent_rgb, white, 0.45))

    rows = bytearray()
    for y in range(height):
        rows.append(0)
        start = y * width * 3
        rows.extend(pixels[start:start + width * 3])
    compressed = zlib.compress(bytes(rows), 9)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + png_chunk(b"tEXt", f"Title\x00{seed} preview".encode("latin-1", "replace"))
        + png_chunk(b"IDAT", compressed)
        + png_chunk(b"IEND", b"")
    )


def export_package_files(app_id: str) -> tuple[str, dict[str, bytes]]:
    app_id = safe_package_id(app_id)
    package_id, files = load_package_from_dir(RUNTIME_APPS_DIR / app_id, app_id)
    files = dict(files)
    manifest: dict[str, Any] = {}
    if "manifest.json" in files:
        manifest = json.loads(files["manifest.json"].decode("utf-8"))
    screenshot = str(manifest.get("screenshot") or "")
    if not screenshot or screenshot.startswith("generated:") or screenshot not in files:
        meta = app_entry(app_id)
        screenshot = "images/screenshot.png"
        files[screenshot] = generated_screenshot_png(app_id, str(meta.get("iconText") or app_id[:1]), str(meta.get("accent") or "#5db7ff"))
        if manifest:
            manifest["screenshot"] = screenshot
            files["manifest.json"] = json.dumps(manifest, ensure_ascii=False, indent=2).encode("utf-8") + b"\n"
    if "README.md" not in files:
        meta = app_entry(app_id)
        files["README.md"] = (
            f"# {meta.get('name') or app_id}\n\n"
            f"{meta.get('description') or meta.get('summary') or 'Huangshan Pi Runtime App.'}\n\n"
            f"- App ID: `{app_id}`\n"
            f"- Category: {meta.get('category') or 'General'}\n"
            f"- Author: {meta.get('author') or 'Huangshan Runtime'}\n"
            f"- Requirements: {', '.join(normalize_requirements(meta.get('requirements'))) or 'Runtime'}\n"
        ).encode("utf-8")
    with package_errors_as_exceptions():
        package_id, files = validate_package(package_id, files)
    validate_screenshot_reference(package_id, files)
    return package_id, files


def zip_package_bytes(package_id: str, files: dict[str, bytes]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        current = time.localtime()
        for path, data in sorted(files.items()):
            info = zipfile.ZipInfo(path, current[:6])
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
        if "README.md" not in files:
            readme = (
                f"# {package_id}\n\n"
                "Huangshan Pi Runtime App package exported from the local VibeBoard App Store.\n"
            )
            info = zipfile.ZipInfo("README.md", current[:6])
            info.external_attr = 0o644 << 16
            archive.writestr(info, readme.encode("utf-8"))
    return buffer.getvalue()


def urlopen_json(url: str, timeout: float = 10.0) -> dict[str, Any]:
    req = urllib.request.Request(url, headers={"user-agent": "VibeBoardAppStore/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def classify_weather(code: int) -> str:
    return WEATHER_CODE_CONDITION.get(code, "cloudy")


def is_default_location(city: str) -> bool:
    compact = city.lower().replace(" ", "").replace("-", "")
    aliases = (
        "nanchang",
        "nanchanghonggutan",
        "honggutan",
        "南昌",
        "南昌市",
        "红谷滩",
        "红谷滩区",
        "南昌红谷滩",
        "南昌市红谷滩区",
    )
    return any(alias in compact for alias in aliases)


def resolve_city(city: str) -> tuple[float, float, str]:
    if is_default_location(city):
        return DEFAULT_LATITUDE, DEFAULT_LONGITUDE, "Nanchang Honggutan, CN"
    params = urllib.parse.urlencode({"name": city, "count": 1, "language": "en", "format": "json"})
    data = urlopen_json(f"https://geocoding-api.open-meteo.com/v1/search?{params}")
    results = data.get("results") or []
    if not results:
        raise ValueError(f"City not found: {city}")
    first = results[0]
    name = first.get("name") or city
    country = first.get("country_code") or first.get("country") or ""
    label = f"{name}, {country}" if country else name
    return float(first["latitude"]), float(first["longitude"]), label


def resolve_weather(payload: dict[str, Any]) -> dict[str, Any]:
    city = str(payload.get("city") or DEFAULT_CITY).strip() or DEFAULT_CITY
    if payload.get("latitude") is not None and payload.get("longitude") is not None:
        lat = float(payload["latitude"])
        lon = float(payload["longitude"])
        label = city if city else "Current Location"
    else:
        lat, lon, label = resolve_city(city)

    params = urllib.parse.urlencode(
        {
            "latitude": f"{lat:.5f}",
            "longitude": f"{lon:.5f}",
            "current": "temperature_2m,relative_humidity_2m,weather_code",
            "timezone": "auto",
        }
    )
    data = urlopen_json(f"https://api.open-meteo.com/v1/forecast?{params}")
    current = data.get("current") or {}
    code = int(current.get("weather_code", 3))
    temp = float(current.get("temperature_2m", 0))
    humidity = float(current.get("relative_humidity_2m", 0))
    return {
        "city": label,
        "latitude": lat,
        "longitude": lon,
        "temperature_c": temp,
        "humidity": humidity,
        "weather_code": code,
        "condition": classify_weather(code),
        "time": current.get("time", ""),
    }


def lua_string(value: str) -> str:
    return json.dumps(str(value), ensure_ascii=True)


def prepare_weather_package(weather: dict[str, Any]) -> Path:
    if isinstance(weather.get("weather"), dict):
        weather = weather["weather"]
    temp_dir = Path(tempfile.mkdtemp(prefix="vibeboard_weather_"))
    src = RUNTIME_APPS_DIR / "weather_pet"
    dst = temp_dir / "weather_pet"
    shutil.copytree(src, dst)
    city = str(weather.get("city") or "Current Location")[:80]
    condition = str(weather.get("condition") or "cloudy")[:24]
    asset_name = condition if condition in WEATHER_ASSETS else "cloudy"
    temp_c = int(round(float(weather.get("temperature_c", 0))))
    humidity = int(round(float(weather.get("humidity", 0))))
    code = int(weather.get("weather_code", 3))
    script = (
        "local root = lv_scr_act()\n"
        "lv_obj_clean(root)\n"
        "lv_obj_set_style_bg_color(root, 0x103749)\n"
        "lv_obj_set_style_text_color(root, 0xf8fafc)\n"
        f"vibe_weather_pet({lua_string(city)}, {lua_string(condition)}, {temp_c}, {humidity}, {code})\n"
        "print(\"[weather_pet] live weather pet requested\")\n"
    )
    (dst / "main.lua").write_text(script, encoding="utf-8")
    manifest_path = dst / "manifest.json"
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["screenshot"] = "generated:weather_pet"
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    asset_dir = dst / "assets" / "weather"
    if asset_dir.exists():
        for path in asset_dir.iterdir():
            if path.suffix.lower() not in {".png", ".bin"}:
                continue
            if path.suffix.lower() == ".png" or path.stem != asset_name:
                path.unlink()
    return dst


def is_transport_exception(exc: BaseException) -> bool:
    return isinstance(exc, (RuntimeTransportError, TimeoutError, asyncio.TimeoutError))


def runtime_json(text: str) -> dict[str, Any]:
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Runtime did not return JSON: {text[-500:]}") from exc
    if not isinstance(data, dict):
        raise RuntimeError("Runtime JSON response was not an object")
    return data


CAPABILITY_ROWS = [
    {
        "id": "display",
        "name": "Display",
        "hw": "disp",
        "apiField": "disp",
        "apiName": "display",
        "statusEndpoint": "/api/runtime/capabilities",
        "examples": ["display_stage", "weather_pet", "game_2048"],
        "manifest": ["display.brightness", "display.size", "display.state"],
    },
    {
        "id": "touch",
        "name": "Touch",
        "hw": "touch",
        "apiField": "touch",
        "apiName": "touch",
        "examples": ["touch_stage", "game_2048"],
        "manifest": ["touch.last", "touch.gesture", "touch.delta"],
    },
    {
        "id": "gpio",
        "name": "GPIO",
        "hw": "gpio",
        "apiField": "gpio",
        "apiName": "gpio",
        "examples": ["gpio_keys_stage"],
        "manifest": ["gpio.key1", "gpio.key2"],
    },
    {
        "id": "rgb",
        "name": "RGB",
        "hw": "rgb",
        "apiField": "rgb",
        "apiName": "rgb",
        "examples": ["rgb_test"],
        "manifest": ["vibe_rgb", "rgb"],
    },
    {
        "id": "voice",
        "name": "Voice",
        "hw": "voice",
        "apiField": "voice",
        "apiName": "voice",
        "examples": ["voice_stage"],
        "manifest": ["voice.ready", "voice.start", "voice.clear"],
    },
    {
        "id": "flow",
        "name": "Flow",
        "hw": "flow",
        "apiField": "flow",
        "apiName": "flow",
        "examples": ["flow_stage"],
        "manifest": ["flow.latest", "flow.payload", "flow.channel"],
    },
    {
        "id": "power",
        "name": "Power",
        "hw": "batt",
        "apiField": "pwr",
        "apiName": "power",
        "examples": ["power_stage"],
        "manifest": ["power.battery", "power.charger"],
        "extraHw": ["chg"],
    },
    {
        "id": "sensors",
        "name": "Sensors",
        "hw": "sens",
        "apiField": "sens",
        "apiName": "sensors",
        "examples": ["sensor_stage", "sensor_dash"],
        "manifest": ["sensor.light", "sensor.acce", "sensor.gyro", "sensor.step"],
    },
]


def matrix_from_capabilities(caps: dict[str, Any] | None) -> list[dict[str, Any]]:
    caps = caps or {}
    hw = caps.get("hw") if isinstance(caps.get("hw"), dict) else {}
    rows: list[dict[str, Any]] = []
    for item in CAPABILITY_ROWS:
        api_field = str(item["apiField"])
        hw_keys = [str(item["hw"]), *[str(value) for value in item.get("extraHw", [])]]
        available = any(int(hw.get(key, 0) or 0) == 1 for key in hw_keys)
        api_version = str(caps.get(api_field) or "")
        rows.append({
            **item,
            "available": available,
            "apiVersion": api_version,
            "transport": {
                "serial": int((caps.get("ins") or {}).get("ser", 0) or 0) == 1 if isinstance(caps.get("ins"), dict) else False,
                "ble": int((caps.get("ins") or {}).get("ble", 0) or 0) == 1 if isinstance(caps.get("ins"), dict) else False,
            },
        })
    return rows


def parse_runtime_status_text(text: str) -> dict[str, Any]:
    lines = [line.strip() for line in str(text or "").splitlines() if line.strip()]
    facts: dict[str, Any] = {"lines": lines[-18:]}
    for line in lines:
        if "[vb_runtime]" not in line:
            continue
        body = line.split("[vb_runtime]", 1)[1].strip()
        if body.startswith("active="):
            facts["active"] = body.split("=", 1)[1].split()[0]
        elif body.startswith("fs="):
            facts["fs"] = body.split("=", 1)[1].split()[0]
        elif body.startswith("transport="):
            facts["runtimeTransport"] = body.split("=", 1)[1].split()[0]
        elif body.startswith("running="):
            facts["running"] = body.split("=", 1)[1].split()[0]
        elif body.startswith("app_manager="):
            facts["appManagerLine"] = body
            values = parse_key_values(body)
            facts["state"] = values.get("state", "")
            facts["last_status"] = values.get("last", "")
            facts["last_error"] = values.get("error", "")
    return facts


def run_runtime_app_status(timeout: float = 18.0) -> tuple[bool, dict[str, Any] | str]:
    try:
        payload = run_transport_operation(
            lambda transport: runtime_json(transport.app_status()),
            lambda transport: transport.app_status(),
            timeout=timeout,
        )
        if isinstance(payload, str):
            payload = runtime_json(payload)
        return True, payload
    except Exception as exc:
        return False, str(exc)


def run_ble_status_text(timeout: float = 18.0) -> tuple[bool, str]:
    try:
        if TRANSPORT_KIND == "serial":
            payload = run_transport_operation(
                lambda transport: transport.read_matching(
                    "vb_runtime_ble_status",
                    lambda text: "[vb_runtime][ble]" in text or "ok ble" in text or "unavailable" in text,
                    timeout=max(6.0, transport.options.final_wait + 4.0),
                    wait=max(transport.options.final_wait, 0.4),
                ),
                lambda transport: transport.command("ble_status"),
                timeout=timeout,
            )
        else:
            payload = run_transport_operation(
                lambda transport: transport.status(),
                lambda transport: transport.command("ble_status"),
                timeout=timeout,
            )
        return True, str(payload)
    except Exception as exc:
        return False, str(exc)


def run_staging_clear(timeout: float = 18.0) -> str:
    return run_transport_operation(
        lambda transport: transport.staging_clear(),
        lambda transport: transport.staging_clear(),
        timeout=timeout,
    )


def runtime_diagnostics() -> dict[str, Any]:
    started = time.time()
    status_ok, status_payload = run_status(timeout=10.0)
    caps_ok, caps_payload = run_runtime_capabilities(timeout=12.0)
    app_ok, app_payload = run_runtime_app_status(timeout=10.0)
    apps_ok, apps_payload = run_runtime_apps(timeout=16.0)
    ble_ok, ble_payload = run_ble_status_text(timeout=10.0)
    status_text = str(status_payload)
    caps = caps_payload if caps_ok and isinstance(caps_payload, dict) else {}
    app_status = app_payload if app_ok and isinstance(app_payload, dict) else {}
    apps = apps_payload if apps_ok and isinstance(apps_payload, dict) else {}
    facts = parse_runtime_status_text(status_text)
    recent_errors = []
    if facts.get("last_error") and facts.get("last_error") != "--":
        recent_errors.append(str(facts["last_error"]))
    if isinstance(app_status, dict) and app_status.get("last_error"):
        recent_errors.append(str(app_status["last_error"]))
    recent_errors = list(dict.fromkeys(item for item in recent_errors if item and item != "--"))
    return {
        "ok": status_ok and caps_ok,
        "transport": TRANSPORT_KIND,
        "durationMs": int((time.time() - started) * 1000),
        "serial": {
            "configured": TRANSPORT_KIND == "serial",
            "port": SERIAL_PORT,
            "baud": SERIAL_BAUD,
            "ok": status_ok,
            "message": transportSummaryText(status_payload),
        },
        "runtime": {
            "ok": status_ok,
            "rawStatus": status_text,
            "facts": facts,
            "appStatus": app_status if app_ok else {"error": transportErrorSummary(app_payload)},
        },
        "sdCard": {
            "ok": caps.get("fs") == 1 or facts.get("fs") == "ready",
            "state": "ready" if caps.get("fs") == 1 or facts.get("fs") == "ready" else "unavailable",
        },
        "ble": {
            "ok": ble_ok,
            "status": ble_payload,
            "configuredName": BLE_NAME,
        },
        "capabilities": caps if caps_ok else {"error": transportErrorSummary(caps_payload)},
        "matrix": matrix_from_capabilities(caps if caps_ok and isinstance(caps, dict) else {}),
        "apps": apps if apps_ok else {"error": transportErrorSummary(apps_payload), "apps": []},
        "recentErrors": recent_errors,
        "checks": {
            "status": {"ok": status_ok, "error": None if status_ok else transportErrorSummary(status_payload)},
            "capabilities": {"ok": caps_ok, "error": None if caps_ok else transportErrorSummary(caps_payload)},
            "appStatus": {"ok": app_ok, "error": None if app_ok else transportErrorSummary(app_payload)},
            "installedApps": {"ok": apps_ok, "error": None if apps_ok else transportErrorSummary(apps_payload)},
            "ble": {"ok": ble_ok, "error": None if ble_ok else transportErrorSummary(ble_payload)},
        },
    }


def transportSummaryText(payload: Any) -> str:
    text = str(payload or "").strip()
    if not text:
        return ""
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return lines[-1] if lines else text[:200]


def transportErrorSummary(payload: Any) -> str:
    text = str(payload or "").strip()
    if not text:
        return "Runtime 状态读取失败，请刷新重试"
    lower = text.lower()
    if "pyserial" in lower:
        return "串口桥缺少 pyserial，请用 SiFli Python 环境启动服务"
    if "did not receive json response" in lower or "expected json response" in lower:
        return "Runtime 未返回完整 JSON，请刷新重试"
    if "did not receive expected serial response" in lower:
        return "串口响应超时，请刷新重试"
    if "timed out" in lower or "timeout" in lower:
        return "Runtime 状态读取超时，请刷新重试"
    if "not connected" in lower or "disconnected" in lower:
        return "设备未连接，请检查串口或 BLE"
    first = next((line.strip() for line in text.splitlines() if line.strip()), text)
    return first[:180] + ("..." if len(first) > 180 else "")


def detected_serial_ports() -> list[dict[str, Any]]:
    proc = subprocess.run(
        [sys.executable, str(FLASH_SCRIPT), "--list-ports"],
        cwd=ROOT_DIR,
        text=True,
        capture_output=True,
        timeout=8,
    )
    ports: list[dict[str, Any]] = []
    for line in proc.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) >= 3:
            ports.append({
                "path": parts[0],
                "role": parts[1],
                "recommended": parts[2] == "recommended",
                "selected": parts[0] == SERIAL_PORT,
            })
    return ports


def serial_options() -> SerialTransportOptions:
    if not SERIAL_PORT:
        raise RuntimeError("serial transport requires --serial-port")
    return SerialTransportOptions(port=SERIAL_PORT, baud=SERIAL_BAUD, final_wait=2.0, echo=False)


def ble_options() -> BLETransportOptions:
    return BLETransportOptions(
        name=BLE_NAME,
        no_cache=BLE_NO_CACHE,
        scan_timeout=8,
        connect_timeout=8,
        response_wait=0.2,
        final_wait=0.8,
        echo=False,
    )


async def run_ble_transport_operation(
    operation: Callable[[AsyncRuntimeTransport], Awaitable[T]],
    *,
    timeout: float = 18.0,
    options: BLETransportOptions | None = None,
) -> T:
    async with BLETransport(options or ble_options()) as transport:
        return await asyncio.wait_for(operation(transport), timeout=timeout)


def run_transport_operation(
    sync_operation: Callable[[SyncRuntimeTransport], T],
    async_operation: Callable[[AsyncRuntimeTransport], Awaitable[T]],
    *,
    timeout: float = 18.0,
) -> T:
    with TRANSPORT_LOCK:
        if TRANSPORT_KIND == "serial":
            with SerialTransport(serial_options()) as transport:
                return sync_operation(transport)
        if PAGER_VOICE_ACTIVE_FILE.exists():
            raise RuntimeTransportError(
                "Pager voice bridges are active; close the voice bridge terminal before using BLE App Store"
            )
        return asyncio.run(run_ble_transport_operation(async_operation, timeout=timeout))


def run_status(timeout: float = 18.0) -> tuple[bool, str]:
    try:
        status = run_transport_operation(
            lambda transport: transport.status(),
            lambda transport: transport.status(),
            timeout=timeout,
        )
        return True, status
    except Exception as exc:
        return False, str(exc)


def run_runtime_apps(timeout: float = 18.0) -> tuple[bool, dict[str, Any] | str]:
    try:
        payload = run_transport_operation(
            lambda transport: runtime_json(transport.apps()),
            lambda transport: transport.apps(),
            timeout=timeout,
        )
        if isinstance(payload, str):
            payload = runtime_json(payload)
        return True, payload
    except Exception as exc:
        return False, str(exc)


def run_runtime_capabilities(timeout: float = 18.0) -> tuple[bool, dict[str, Any] | str]:
    try:
        payload = run_transport_operation(
            lambda transport: runtime_json(transport.capabilities()),
            lambda transport: transport.capabilities(),
            timeout=timeout,
        )
        if isinstance(payload, str):
            payload = runtime_json(payload)
        return True, payload
    except Exception as exc:
        return False, str(exc)


def run_runtime_app_command(action: str, app_id: str | None = None, timeout: float = 18.0) -> str:
    app_id = safe_package_id(app_id) if app_id else None

    def run_sync(transport: SyncRuntimeTransport) -> str:
        if action == "launch" and app_id:
            return transport.launch_app(app_id)
        if action == "delete" and app_id:
            return transport.delete_app(app_id)
        if action == "stop":
            return transport.stop_app()
        raise RuntimeError(f"unsupported runtime app action: {action}")

    async def run_async(transport: AsyncRuntimeTransport) -> str:
        if action == "launch" and app_id:
            return await transport.launch_app(app_id)
        if action == "delete" and app_id:
            return await transport.delete_app(app_id)
        if action == "stop":
            return await transport.stop_app()
        raise RuntimeError(f"unsupported runtime app action: {action}")

    return run_transport_operation(run_sync, run_async, timeout=timeout)


class ImportedPackage:
    def __init__(self, token: str, package_id: str, files: dict[str, bytes], metadata: dict[str, Any]) -> None:
        self.token = token
        self.package_id = package_id
        self.files = files
        self.metadata = metadata
        self.created_at = time.time()

    def to_dict(self) -> dict[str, Any]:
        return {
            "token": self.token,
            "package": self.metadata,
            "created_at": self.created_at,
        }


class Job:
    def __init__(self, job_id: str, app_id: str, kind: str = "install") -> None:
        self.job_id = job_id
        self.app_id = app_id
        self.kind = kind
        self.status = "queued"
        self.progress = 0
        self.message = "queued"
        self.log: list[str] = []
        self.created_at = time.time()

    def append(self, line: str) -> None:
        self.log.append(line)
        if len(self.log) > JOB_LOG_MAX_LINES:
            self.log = self.log[-JOB_LOG_MAX_LINES:]

    def to_dict(self) -> dict[str, Any]:
        return {
            "job_id": self.job_id,
            "app_id": self.app_id,
            "kind": self.kind,
            "status": self.status,
            "progress": self.progress,
            "message": self.message,
            "log": self.log,
        }


class StoreState:
    def __init__(self) -> None:
        self.jobs: dict[str, Job] = {}
        self.imports: dict[str, ImportedPackage] = {}
        self.install_requests: dict[str, str] = {}
        self.lock = threading.Lock()

    def start_job(self, kind: str, app_id: str, target, *args, request_key: str = "") -> Job:
        job = Job(f"job-{uuid.uuid4().hex[:12]}", app_id, kind)
        with self.lock:
            if request_key:
                existing_id = self.install_requests.get(request_key)
                existing = self.jobs.get(existing_id or "")
                if existing:
                    return existing
            self.jobs[job.job_id] = job
            if request_key:
                self.install_requests[request_key] = job.job_id
        thread = threading.Thread(target=target, args=(job, *args), daemon=True)
        thread.start()
        return job

    def create_job(self, app_id: str, weather: dict[str, Any] | None, request_key: str = "") -> Job:
        return self.start_job("install", app_id, self.run_install, weather, request_key=request_key)

    def create_import_install_job(self, package: ImportedPackage) -> Job:
        return self.start_job("install_import", package.package_id, self.run_install_files, package.package_id, package.files)

    def create_staging_clear_job(self) -> Job:
        return self.start_job("staging_clear", "staging", self.run_staging_clear)

    def create_reinstall_examples_job(self, app_ids: list[str]) -> Job:
        return self.start_job("reinstall_examples", "examples", self.run_reinstall_examples, app_ids)

    def create_flash_runtime_job(self, port: str) -> Job:
        return self.start_job("flash_runtime", "runtime", self.run_flash_runtime, port)

    def get_job(self, job_id: str) -> Job | None:
        with self.lock:
            return self.jobs.get(job_id)

    def remember_import(self, package_id: str, files: dict[str, bytes], metadata: dict[str, Any]) -> ImportedPackage:
        token = f"pkg-{uuid.uuid4().hex[:16]}"
        package = ImportedPackage(token, package_id, files, metadata)
        with self.lock:
            self.imports[token] = package
            cutoff = time.time() - 3600
            stale = [key for key, item in self.imports.items() if item.created_at < cutoff]
            for key in stale:
                del self.imports[key]
        return package

    def get_import(self, token: str) -> ImportedPackage | None:
        with self.lock:
            return self.imports.get(token)

    def install_with_runtime_transport(self, job: Job, package_id: str, files: dict[str, bytes], attempt: int, on_progress) -> None:
        job.append(
            f"runtime_transport {TRANSPORT_KIND} install app={package_id}"
            + (f" port={SERIAL_PORT}" if TRANSPORT_KIND == "serial" else f" name={BLE_NAME}")
            + (f"  # attempt {attempt}" if attempt > 1 else "")
        )
        job.progress = max(job.progress, 24)
        job.message = f"connecting {TRANSPORT_KIND}"

        def run_sync(transport: SyncRuntimeTransport) -> str:
            return transport.install_package(
                package_id,
                files,
                chunk_bytes=SERIAL_INSTALL_CHUNK_BYTES,
                progress=on_progress,
            )

        async def run_async(transport: AsyncRuntimeTransport) -> str:
            return await transport.install_package(
                package_id,
                files,
                chunk_bytes=BLE_INSTALL_CHUNK_BYTES,
                progress=on_progress,
            )

        job.progress = max(job.progress, 28)
        job.message = "connected"
        if TRANSPORT_KIND == "serial":
            install_timeout = 90.0
        else:
            chunks = sum(
                (len(data) + BLE_INSTALL_CHUNK_BYTES - 1) // BLE_INSTALL_CHUNK_BYTES
                for data in files.values()
            ) + len(files) + 2
            install_timeout = max(90.0, min(900.0, 30.0 + chunks * 0.45))
        status = run_transport_operation(run_sync, run_async, timeout=install_timeout)
        job.append(status)
        job.progress = 98
        job.message = "confirmed active"

    def install_with_transport(self, job: Job, package_id: str, files: dict[str, bytes], attempt: int) -> None:
        def on_progress(command: str, index: int, total: int) -> None:
            if command.startswith("vb_runtime_install_begin "):
                job.append("install_begin")
                job.progress = max(job.progress, 40)
                job.message = "uploading"
            elif command.startswith("vb_runtime_install_file "):
                if index == 2 or index == total or index % 25 == 0:
                    job.append(f"install_file {index}/{total}")
                job.progress = min(88, max(job.progress, 40 + int(index / max(total, 1) * 45)))
                job.message = "uploading files"
            elif command.startswith("vb_runtime_install_end "):
                job.append("install_end")
                job.progress = max(job.progress, 92)
                job.message = "finalizing"

        self.install_with_runtime_transport(job, package_id, files, attempt, on_progress)

    def validate_and_load_package(self, job: Job, package_dir: Path) -> tuple[str, dict[str, bytes]]:
        job.append(f"runtime_package validate app={job.app_id} dir={package_dir}")
        try:
            with package_errors_as_exceptions():
                package_id, files = load_package_from_dir(package_dir, job.app_id)
        except RuntimePackageError as exc:
            raise RuntimeError(f"package validation failed for {job.app_id}: {exc}") from exc
        byte_count = sum(len(data) for data in files.values())
        job.append(f"ok package app={package_id} files={len(files)} bytes={byte_count}")
        return package_id, files

    def run_install_files(self, job: Job, package_id: str, files: dict[str, bytes]) -> None:
        try:
            job.status = "running"
            job.progress = max(job.progress, 16)
            job.message = "validating package"
            with package_errors_as_exceptions():
                package_id, files = validate_package(package_id, files)
            job.append(f"ok package app={package_id} files={len(files)} bytes={sum(len(data) for data in files.values())}")
            try:
                self.install_with_transport(job, package_id, files, 1)
            except Exception as exc:
                job.append(f"{TRANSPORT_KIND} install attempt 1 failed: {exc}; retrying once after reconnect delay")
                job.progress = 18
                job.message = f"retrying {TRANSPORT_KIND}"
                time.sleep(2.5)
                self.install_with_transport(job, package_id, files, 2)
            job.status = "done"
            job.progress = 100
            job.message = f"installed {package_id}"
        except Exception as exc:
            job.status = "failed"
            job.message = str(exc)
            job.append(f"ERROR: {exc}")

    def run_install(self, job: Job, weather: dict[str, Any] | None) -> None:
        temp_root: Path | None = None
        try:
            job.status = "running"
            job.progress = 8
            job.message = "preparing package"
            if job.app_id == "weather_pet":
                if not weather:
                    raise ValueError("weather_pet requires weather data")
                package_dir = prepare_weather_package(weather)
                temp_root = package_dir.parent
                job.append(f"prepared weather package: {weather.get('city')} {weather.get('condition')}")
            else:
                package_dir = RUNTIME_APPS_DIR / job.app_id
            if not package_dir.exists():
                raise ValueError(f"unknown app: {job.app_id}")

            job.progress = 14
            job.message = "validating package"
            package_id, files = self.validate_and_load_package(job, package_dir)

            try:
                self.install_with_transport(job, package_id, files, 1)
            except Exception as exc:
                job.append(f"{TRANSPORT_KIND} install attempt 1 failed: {exc}; retrying once after reconnect delay")
                job.progress = 18
                job.message = f"retrying {TRANSPORT_KIND}"
                time.sleep(2.5)
                self.install_with_transport(job, package_id, files, 2)
            job.status = "done"
            job.progress = 100
            job.message = f"installed {job.app_id}"
        except RuntimeTransportError as exc:
            job.status = "failed"
            job.message = str(exc)
            job.append(f"ERROR: {exc}")
        except Exception as exc:
            job.status = "failed"
            job.message = str(exc)
            job.append(f"ERROR: {exc}")
        finally:
            if temp_root:
                shutil.rmtree(temp_root, ignore_errors=True)

    def run_staging_clear(self, job: Job) -> None:
        try:
            job.status = "running"
            job.progress = 20
            job.message = "clearing staging"
            status = run_staging_clear(timeout=18.0)
            job.append(status)
            job.progress = 100
            job.status = "done"
            job.message = "staging cleared"
        except Exception as exc:
            job.status = "failed"
            job.message = str(exc)
            job.append(f"ERROR: {exc}")

    def run_reinstall_examples(self, job: Job, app_ids: list[str]) -> None:
        try:
            job.status = "running"
            if not app_ids:
                raise RuntimeError("no example apps selected")
            for index, app_id in enumerate(app_ids, start=1):
                safe_id = safe_package_id(app_id)
                package_dir = RUNTIME_APPS_DIR / safe_id
                job.app_id = safe_id
                job.message = f"installing example {safe_id}"
                job.progress = int((index - 1) / len(app_ids) * 92)
                package_id, files = self.validate_and_load_package(job, package_dir)
                self.install_with_transport(job, package_id, files, 1)
            job.app_id = "examples"
            job.progress = 100
            job.status = "done"
            job.message = f"reinstalled {len(app_ids)} example apps"
        except Exception as exc:
            job.status = "failed"
            job.message = str(exc)
            job.append(f"ERROR: {exc}")

    def run_flash_runtime(self, job: Job, port: str) -> None:
        try:
            if TRANSPORT_KIND != "serial":
                raise RuntimeError("Runtime recovery flash requires --transport serial")
            port = port or SERIAL_PORT or ""
            if not port:
                raise RuntimeError("Runtime recovery flash requires a serial port")
            job.status = "running"
            job.progress = 4
            job.message = "validating flash artifacts"
            command = [
                sys.executable,
                str(FLASH_SCRIPT),
                "--port",
                port,
                "--board",
                os.environ.get("BOARD", "sf32lb52-lchspi-ulp"),
                "--confirm-boot",
            ]
            job.append(" ".join(command))
            proc = subprocess.Popen(
                command,
                cwd=ROOT_DIR,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            if proc.stdout is None:
                proc.kill()
                raise RuntimeError("flash.py did not provide an output stream")
            for line in proc.stdout:
                text = line.rstrip()
                if text:
                    job.append(text)
                if "[flash] attempt" in text:
                    job.progress = max(job.progress, 30)
                    job.message = "flashing Runtime"
                elif "[boot]" in text:
                    job.progress = max(job.progress, 88)
                    job.message = "confirming boot"
            code = proc.wait()
            if code != 0:
                raise RuntimeError(f"flash.py exited with {code}")
            job.progress = 100
            job.status = "done"
            job.message = "Runtime recovered"
        except Exception as exc:
            job.status = "failed"
            job.message = str(exc)
            job.append(f"ERROR: {exc}")


STATE = StoreState()


class Handler(BaseHTTPRequestHandler):
    server_version = "VibeBoardAppStore/1.0"

    def log_message(self, fmt: str, *args: Any) -> None:
        print("[app-store]", fmt % args)

    def read_json(self) -> dict[str, Any]:
        try:
            length = int(self.headers.get("content-length") or "0")
        except ValueError as exc:
            raise ValueError("invalid content-length") from exc
        if length > 65536:
            raise ValueError("request body too large")
        if length == 0:
            return {}
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def read_body(self, max_bytes: int = IMPORT_MAX_BYTES) -> bytes:
        try:
            length = int(self.headers.get("content-length") or "0")
        except ValueError as exc:
            raise ValueError("invalid content-length") from exc
        if length <= 0:
            raise ValueError("request body is empty")
        if length > max_bytes:
            raise ValueError(f"request body too large; max {max_bytes} bytes")
        return self.rfile.read(length)

    def send_bytes(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("content-type", content_type)
        self.send_header("content-length", str(len(body)))
        self.send_header("cache-control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_download(self, filename: str, body: bytes, content_type: str) -> None:
        self.send_response(200)
        self.send_header("content-type", content_type)
        self.send_header("content-length", str(len(body)))
        self.send_header("cache-control", "no-store")
        self.send_header("content-disposition", f"attachment; filename=\"{filename}\"")
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, status: int, data: Any) -> None:
        self.send_bytes(status, json_bytes(data), "application/json; charset=utf-8")

    def send_error_json(self, status: int, message: str) -> None:
        self.send_json(status, {"error": message})

    def do_GET(self) -> None:
        try:
            path = request_path(self.path)
            if path in {"/", "/index.html", "/recovery", "/capabilities"}:
                self.send_bytes(200, HTML.encode("utf-8"), "text/html; charset=utf-8")
            elif path == "/api/apps":
                self.send_json(200, {"apps": list_apps()})
            elif path == "/api/capabilities/matrix":
                ok, payload = run_runtime_capabilities()
                caps = payload if ok and isinstance(payload, dict) else {}
                self.send_json(200, {
                    "ok": ok,
                    "transport": TRANSPORT_KIND,
                    "capabilities": caps,
                    "matrix": matrix_from_capabilities(caps),
                    "errorSummary": None if ok else transportErrorSummary(payload),
                })
            elif path == "/api/recovery/diagnostics":
                data = runtime_diagnostics()
                try:
                    data["serial"]["ports"] = detected_serial_ports()
                except Exception as exc:
                    data["serial"]["ports"] = []
                    data["serial"]["port_error"] = str(exc)
                self.send_json(200, data)
            elif path.startswith("/api/apps/") and path.endswith("/export"):
                app_id = urllib.parse.unquote(path.split("/")[3])
                if app_id not in APP_ORDER:
                    self.send_error_json(404, "unknown app")
                    return
                package_id, files = export_package_files(app_id)
                self.send_download(f"{package_id}.happ", zip_package_bytes(package_id, files), "application/zip")
            elif path.startswith("/api/apps/") and path.endswith("/generated-screenshot.png"):
                app_id = urllib.parse.unquote(path.split("/")[3])
                if app_id not in APP_ORDER:
                    self.send_error_json(404, "unknown app")
                    return
                meta = app_entry(app_id)
                data = generated_screenshot_png(app_id, str(meta.get("iconText") or app_id[:1]), str(meta.get("accent") or "#5db7ff"))
                self.send_bytes(200, data, "image/png")
            elif path.startswith("/api/apps/") and "/asset/" in path:
                tail = path.removeprefix("/api/apps/")
                app_id_text, sep, asset_path = tail.partition("/asset/")
                if not sep:
                    self.send_error_json(404, "asset not found")
                    return
                app_id = urllib.parse.unquote(app_id_text)
                data, content_type = local_app_asset(app_id, asset_path)
                self.send_bytes(200, data, content_type)
            elif path in ("/api/ble/status", "/api/transport/status"):
                ok, output = run_status()
                self.send_json(200, {"ok": ok, "transport": TRANSPORT_KIND, "status": output, "statusSummary": transportErrorSummary(output) if not ok else transportSummaryText(output)})
            elif path == "/api/runtime/apps":
                ok, payload = run_runtime_apps()
                if ok:
                    self.send_json(200, {
                        "ok": True,
                        "transport": TRANSPORT_KIND,
                        "runtime": normalize_runtime_app_screenshots(payload),
                    })
                else:
                    self.send_json(200, {"ok": False, "transport": TRANSPORT_KIND, "error": payload, "errorSummary": transportErrorSummary(payload)})
            elif path == "/api/runtime/capabilities":
                ok, payload = run_runtime_capabilities()
                if ok:
                    self.send_json(200, {"ok": True, "transport": TRANSPORT_KIND, "capabilities": payload})
                else:
                    self.send_json(200, {"ok": False, "transport": TRANSPORT_KIND, "error": payload, "errorSummary": transportErrorSummary(payload)})
            elif path.startswith("/api/jobs/"):
                job_id = urllib.parse.unquote(path.rsplit("/", 1)[-1])
                job = STATE.get_job(job_id)
                if not job:
                    self.send_error_json(404, "job not found")
                    return
                self.send_json(200, job.to_dict())
            else:
                self.send_error_json(404, "not found")
        except Exception as exc:
            self.send_error_json(500, str(exc))

    def do_POST(self) -> None:
        try:
            path = request_path(self.path)
            if path == "/api/weather/resolve":
                weather = resolve_weather(self.read_json())
                self.send_json(200, {"weather": weather})
                return
            if path == "/api/apps/import":
                package_id, files, screenshot = package_from_archive(self.read_body())
                metadata = package_metadata(package_id, files)
                metadata["screenshotCheck"] = screenshot
                package = STATE.remember_import(package_id, files, metadata)
                self.send_json(200, {"ok": True, **package.to_dict()})
                return
            if path == "/api/apps/plan":
                payload = self.read_json()
                if not isinstance(payload, dict):
                    self.send_error_json(400, "plan request must be a JSON object")
                    return
                plan = payload.get("plan", payload)
                if isinstance(plan, str):
                    plan = json.loads(plan)
                try:
                    result = build_app_plan_package(plan)
                    screenshot = validate_screenshot_reference(result.app_id, result.files)
                    metadata = package_metadata(result.app_id, result.files)
                    metadata.update(result.metadata)
                    metadata["screenshotCheck"] = screenshot
                    package = STATE.remember_import(result.app_id, result.files, metadata)
                except (RuntimeAppPlanError, RuntimePackageError, ValueError) as exc:
                    self.send_error_json(400, str(exc))
                    return
                self.send_json(200, {"ok": True, "source": "plan", **package.to_dict()})
                return
            if path == "/api/apps/import/install":
                payload = self.read_json()
                token = str(payload.get("token") or "")
                package = STATE.get_import(token)
                if not package:
                    self.send_error_json(404, "import token not found or expired")
                    return
                job = STATE.create_import_install_job(package)
                self.send_json(202, {"job_id": job.job_id, "app_id": package.package_id})
                return
            if path.startswith("/api/apps/") and path.endswith("/install"):
                app_id = urllib.parse.unquote(path.split("/")[3])
                if app_id not in APP_ORDER:
                    self.send_error_json(404, "unknown app")
                    return
                payload = self.read_json()
                request_key = str(payload.get("request_id") or "").strip()
                if len(request_key) > 80 or any(not (char.isalnum() or char in "-_") for char in request_key):
                    self.send_error_json(400, "invalid install request_id")
                    return
                weather = payload.get("weather")
                if isinstance(weather, dict) and isinstance(weather.get("weather"), dict):
                    weather = weather["weather"]
                elif app_id == "weather_pet" and not weather:
                    weather_keys = {"city", "latitude", "longitude", "temperature_c", "humidity", "weather_code", "condition"}
                    if weather_keys.intersection(payload):
                        weather = payload
                job = STATE.create_job(app_id, weather if isinstance(weather, dict) else None, request_key=request_key)
                self.send_json(202, {"job_id": job.job_id})
                return
            if path == "/api/recovery/staging-clear":
                job = STATE.create_staging_clear_job()
                self.send_json(202, {"job_id": job.job_id})
                return
            if path == "/api/recovery/reinstall-examples":
                payload = self.read_json()
                apps = payload.get("apps")
                app_ids = [safe_package_id(str(item)) for item in apps] if isinstance(apps, list) else APP_ORDER[:]
                for app_id in app_ids:
                    if app_id not in APP_ORDER or not (RUNTIME_APPS_DIR / app_id).is_dir():
                        raise ValueError(f"unknown example app: {app_id}")
                job = STATE.create_reinstall_examples_job(app_ids)
                self.send_json(202, {"job_id": job.job_id, "apps": app_ids})
                return
            if path == "/api/recovery/flash-runtime":
                payload = self.read_json()
                confirm = str(payload.get("confirm") or "")
                if confirm != "FLASH_RUNTIME":
                    self.send_error_json(400, "confirm must be FLASH_RUNTIME")
                    return
                port = str(payload.get("port") or SERIAL_PORT or "")
                job = STATE.create_flash_runtime_job(port)
                self.send_json(202, {"job_id": job.job_id})
                return
            if path.startswith("/api/runtime/apps/"):
                parts = path.split("/")
                if len(parts) == 6 and parts[5] in {"launch", "delete"}:
                    app_id = urllib.parse.unquote(parts[4])
                    status = run_runtime_app_command(parts[5], app_id)
                    self.send_json(200, {"ok": True, "transport": TRANSPORT_KIND, "status": status})
                    return
                if path == "/api/runtime/apps/stop":
                    status = run_runtime_app_command("stop")
                    self.send_json(200, {"ok": True, "transport": TRANSPORT_KIND, "status": status})
                    return
            self.send_error_json(404, "not found")
        except urllib.error.URLError as exc:
            self.send_error_json(502, f"weather network failed: {exc}")
        except Exception as exc:
            if is_transport_exception(exc):
                self.send_error_json(502, str(exc))
            else:
                self.send_error_json(400, str(exc))


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the local VibeBoard App Store.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--transport", choices=("ble", "serial"), default="ble", help="Runtime transport used for status/install.")
    parser.add_argument("--serial-port", help="Serial port used when --transport serial is selected.")
    parser.add_argument("--serial-baud", type=int, default=1_000_000)
    parser.add_argument("--ble-name", default=DEFAULT_DEVICE_NAME, help="BLE local name used when --transport ble is selected.")
    parser.add_argument("--ble-no-cache", action="store_true", help="Ignore cached BLE peripheral identifier/address.")
    parser.add_argument("--no-open", action="store_true")
    parser.add_argument("--self-test", action="store_true", help="Run offline helper checks and exit.")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return 0

    global TRANSPORT_KIND, SERIAL_PORT, SERIAL_BAUD, BLE_NAME, BLE_NO_CACHE
    TRANSPORT_KIND = args.transport
    SERIAL_PORT = args.serial_port
    SERIAL_BAUD = args.serial_baud
    BLE_NAME = args.ble_name
    BLE_NO_CACHE = args.ble_no_cache
    if TRANSPORT_KIND == "serial" and not SERIAL_PORT:
        parser.error("--transport serial requires --serial-port")
    if TRANSPORT_KIND == "ble" and not BLE_NAME:
        parser.error("--transport ble requires a non-empty --ble-name")
    if TRANSPORT_KIND == "ble":
        try:
            ensure_bleak()
        except RuntimeTransportError as exc:
            parser.exit(2, f"{exc}\n")

    try:
        server = ThreadingHTTPServer((args.host, args.port), Handler)
    except OSError:
        if args.port != 0:
            server = ThreadingHTTPServer((args.host, 0), Handler)
        else:
            raise
    url = f"http://{args.host}:{server.server_port}"
    detail = f" port={SERIAL_PORT}" if TRANSPORT_KIND == "serial" else f" ble_name={BLE_NAME}" + (" no_cache=1" if BLE_NO_CACHE else "")
    print(f"VibeBoard App Store: {url} transport={TRANSPORT_KIND}{detail}", flush=True)
    if not args.no_open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
