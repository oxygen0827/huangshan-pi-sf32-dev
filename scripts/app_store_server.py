#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ast
import asyncio
import json
import shutil
import sys
import tempfile
import threading
import time
import urllib.error
import uuid
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Awaitable, Callable, TypeVar

from runtime_package import RuntimePackageError, load_package_from_dir, package_errors_as_exceptions, safe_package_id
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
)


ROOT_DIR = Path(__file__).resolve().parents[1]
RUNTIME_APPS_DIR = ROOT_DIR / "scripts" / "runtime_apps"
APP_ORDER = ["weather_pet", "auto_snake", "sensor_stage"]
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
    print("app_store_server self-test ok")


APP_META = {
    "weather_pet": {
        "summary": "真实天气生成的可爱动画天气角色。",
        "accent": "#fbbf24",
        "requiresWeather": True,
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


HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
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
      justify-content: flex-end;
      color: var(--muted);
      font-size: 14px;
    }
    .dot { width: 10px; height: 10px; border-radius: 50%; background: var(--warn); box-shadow: 0 0 18px currentColor; }
    .dot.ok { background: var(--ok); }
    .dot.bad { background: var(--bad); }
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
    .weather-row { display: grid; grid-template-columns: 1fr 1fr auto; gap: 10px; align-items: end; }
    label { display: block; color: var(--muted); font-size: 12px; margin-bottom: 6px; }
    input {
      width: 100%;
      height: 38px;
      border-radius: 6px;
      border: 1px solid var(--line);
      background: #0d141a;
      color: var(--text);
      padding: 0 11px;
      font-size: 14px;
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
    .apps {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 16px;
    }
    .app { padding: 18px; position: relative; overflow: hidden; }
    .app::before {
      content: "";
      position: absolute;
      inset: 0 0 auto;
      height: 4px;
      background: var(--accent, var(--blue));
    }
    .app h3 { margin: 4px 0 8px; font-size: 21px; }
    .app p { margin: 0 0 18px; color: var(--muted); min-height: 44px; font-size: 14px; line-height: 1.45; }
    .app .meta { display: flex; align-items: center; justify-content: space-between; gap: 10px; margin-bottom: 16px; color: var(--muted); font-size: 12px; }
    .badge { border: 1px solid var(--line); border-radius: 999px; padding: 4px 9px; }
    .app button { width: 100%; background: var(--accent, var(--blue)); }
    .section-head { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin: 24px 0 12px; }
    .section-head h2 { margin: 0; font-size: 19px; }
    .runtime-list { display: grid; gap: 8px; }
    .runtime-row { display: grid; grid-template-columns: minmax(140px, 1fr) 120px auto; gap: 10px; align-items: center; padding: 10px 12px; background: #111a22; border: 1px solid var(--line); border-radius: 8px; }
    .runtime-name { font-weight: 700; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .runtime-state { color: var(--muted); font-size: 12px; }
    .runtime-actions { display: flex; gap: 8px; justify-content: flex-end; }
    .runtime-actions button { min-height: 32px; padding: 0 10px; }
    button.danger { background: var(--bad); color: #22050b; }
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
    .bar { height: 100%; width: 0%; background: var(--ok); transition: width 0.2s ease; }
    @media (max-width: 860px) {
      main { width: min(100vw - 28px, 680px); padding-top: 18px; }
      header, .topgrid { grid-template-columns: 1fr; display: grid; }
      .statusbar { justify-content: flex-start; min-width: 0; }
      .apps { grid-template-columns: 1fr; }
      .weather-row { grid-template-columns: 1fr; }
      .runtime-row { grid-template-columns: 1fr; align-items: stretch; }
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
        <div class="progress"><div id="progressBar" class="bar"></div></div>
        <div id="jobText" class="hint">等待选择 App。</div>
        <div id="capabilityHint" class="hint">等待 Runtime 能力握手。</div>
      </div>
    </section>

    <div class="section-head"><h2>本地 App Store</h2></div>
    <section id="apps" class="apps"></section>

    <div class="section-head">
      <h2>板上 App Manager</h2>
      <div class="runtime-actions">
        <button id="runtimeStop" class="danger">停止当前</button>
        <button id="runtimeRefresh" class="secondary">刷新</button>
      </div>
    </div>
    <section id="runtimeApps" class="runtime-list"></section>

    <section id="log" class="log">ready
</section>
  </main>

  <script>
    const state = { apps: [], runtimeApps: [], capabilities: null, weather: null, installing: false, runtimeBusy: false };
    const $ = id => document.getElementById(id);
    const esc = value => String(value ?? "").replace(/[&<>"']/g, ch => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#39;"
    }[ch]));
    const log = message => {
      const box = $("log");
      const time = new Date().toLocaleTimeString();
      box.textContent += `[${time}] ${message}\n`;
      box.scrollTop = box.scrollHeight;
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
    const setTransport = (ok, text) => {
      $("transportDot").className = `dot ${ok ? "ok" : "bad"}`;
      $("transportText").textContent = text;
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
        setTransport(data.ok, data.status || "Transport 已连接");
      } catch (err) {
        setTransport(false, err.message);
      }
    };
    const refreshCapabilities = async () => {
      try {
        const data = await api("/api/runtime/capabilities");
        if (!data.ok) throw new Error(data.error || "Runtime capabilities unavailable");
        state.capabilities = data.capabilities;
        $("capabilityHint").textContent = capabilitySummary(state.capabilities);
        log(`capabilities ${state.capabilities.api || "unknown"}`);
      } catch (err) {
        $("capabilityHint").textContent = err.message;
        log(`capabilities failed: ${err.message}`);
      }
    };
    const conditionLabel = item => `${item.city || "Current"} ${Math.round(item.temperature_c)}C ${item.condition} RH ${Math.round(item.humidity)}%`;
    const runtimeAppsPayload = data => data.runtime?.apps || [];
    const runtimeActive = data => data.runtime?.active || "";
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
        $("weatherHint").textContent = err.message;
        log(`weather failed: ${err.message}`);
      } finally {
        $("weatherBtn").disabled = false;
      }
    };
    const renderApps = () => {
      $("apps").innerHTML = state.apps.map(app => `
        <article class="app" style="--accent:${esc(app.accent)}">
          <div class="meta"><span class="badge">${esc(app.id)}</span><span>${app.requiresWeather ? "需要天气" : "离线可装"}</span></div>
          <h3>${esc(app.name)}</h3>
          <p>${esc(app.summary)}</p>
          <button data-install="${esc(app.id)}" ${state.installing ? "disabled" : ""}>安装到黄山派</button>
        </article>
      `).join("");
      document.querySelectorAll("[data-install]").forEach(button => {
        button.addEventListener("click", () => installApp(button.dataset.install));
      });
    };
    const renderRuntimeApps = () => {
      if (!state.runtimeApps.length) {
        $("runtimeApps").innerHTML = `<div class="hint">尚未读取到板上 App。</div>`;
        return;
      }
      $("runtimeApps").innerHTML = state.runtimeApps.map(app => `
        <div class="runtime-row">
          <div class="runtime-name">${app.active ? "* " : ""}${esc(app.name || app.id)}</div>
          <div class="runtime-state">${esc(app.id)} · ${app.compatible ? "兼容" : "不兼容"}</div>
          <div class="runtime-actions">
            <button data-runtime-launch="${esc(app.id)}" ${state.runtimeBusy ? "disabled" : ""}>启动</button>
            <button data-runtime-delete="${esc(app.id)}" class="danger" ${state.runtimeBusy || app.active ? "disabled" : ""}>删除</button>
          </div>
        </div>
      `).join("");
      document.querySelectorAll("[data-runtime-launch]").forEach(button => {
        button.addEventListener("click", () => runtimeAction("launch", button.dataset.runtimeLaunch));
      });
      document.querySelectorAll("[data-runtime-delete]").forEach(button => {
        button.addEventListener("click", () => runtimeAction("delete", button.dataset.runtimeDelete));
      });
    };
    const refreshRuntimeApps = async () => {
      try {
        const data = await api("/api/runtime/apps");
        if (!data.ok) throw new Error(data.error || "Runtime App Manager unavailable");
        state.runtimeApps = runtimeAppsPayload(data);
        renderRuntimeApps();
        log(`runtime apps ${state.runtimeApps.length} active=${runtimeActive(data)}`);
      } catch (err) {
        $("runtimeApps").innerHTML = `<div class="hint">${esc(err.message)}</div>`;
        log(`runtime apps failed: ${err.message}`);
      }
    };
    const runtimeAction = async (action, appId) => {
      state.runtimeBusy = true;
      renderRuntimeApps();
      try {
        const path = action === "stop" ? "/api/runtime/apps/stop" : `/api/runtime/apps/${encodeURIComponent(appId)}/${action}`;
        const data = await api(path, { method: "POST", body: "{}" });
        log(`${action} ${appId || "active"}: ${data.status}`);
        await refreshTransport();
        await refreshRuntimeApps();
      } catch (err) {
        log(`${action} failed: ${err.message}`);
      } finally {
        state.runtimeBusy = false;
        renderRuntimeApps();
      }
    };
    const pollJob = async jobId => {
      let done = false;
      while (!done) {
        await new Promise(r => setTimeout(r, 700));
        const job = await api(`/api/jobs/${jobId}`);
        $("progressBar").style.width = `${job.progress || 0}%`;
        $("jobText").textContent = job.message || job.status;
        $("log").textContent = job.log.join("\n") + "\n";
        $("log").scrollTop = $("log").scrollHeight;
        done = job.status === "done" || job.status === "failed";
        if (done) {
          if (job.status === "done") log(`install complete: ${job.app_id}`);
          else log(`install failed: ${job.message}`);
        }
      }
    };
    const installApp = async appId => {
      const app = state.apps.find(item => item.id === appId);
      if (app?.requiresWeather && !state.weather) {
        await fetchWeather();
        if (!state.weather) return;
      }
      state.installing = true;
      renderApps();
      $("progressBar").style.width = "3%";
      $("jobText").textContent = `准备安装 ${appId}`;
      try {
        const data = await api(`/api/apps/${appId}/install`, {
          method: "POST",
          body: JSON.stringify({ weather: state.weather })
        });
        await pollJob(data.job_id);
        await refreshTransport();
        await refreshRuntimeApps();
      } catch (err) {
        log(`install request failed: ${err.message}`);
        $("jobText").textContent = err.message;
      } finally {
        state.installing = false;
        renderApps();
      }
    };
    const boot = async () => {
      const apps = await api("/api/apps");
      state.apps = apps.apps;
      renderApps();
      await refreshTransport();
      await refreshCapabilities();
      await refreshRuntimeApps();
      fetchWeather();
      setInterval(refreshTransport, 8000);
      $("weatherBtn").addEventListener("click", fetchWeather);
      $("runtimeRefresh").addEventListener("click", refreshRuntimeApps);
      $("runtimeStop").addEventListener("click", () => runtimeAction("stop"));
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


def app_entry(app_id: str) -> dict[str, Any]:
    app_dir = RUNTIME_APPS_DIR / app_id
    manifest = read_json(app_dir / "manifest.json")
    meta = APP_META.get(app_id, {})
    return {
        "id": app_id,
        "name": manifest.get("name", app_id),
        "description": manifest.get("description", ""),
        "summary": meta.get("summary") or manifest.get("description", ""),
        "accent": meta.get("accent", "#5db7ff"),
        "requiresWeather": bool(meta.get("requiresWeather")),
    }


def list_apps() -> list[dict[str, Any]]:
    apps = []
    for app_id in APP_ORDER:
        if (RUNTIME_APPS_DIR / app_id / "manifest.json").exists():
            apps.append(app_entry(app_id))
    return apps


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
    asset_dir = dst / "assets" / "weather"
    if asset_dir.exists():
        for path in asset_dir.iterdir():
            if path.suffix.lower() not in {".png", ".bin"}:
                continue
            if path.stem != asset_name:
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


class Job:
    def __init__(self, job_id: str, app_id: str) -> None:
        self.job_id = job_id
        self.app_id = app_id
        self.status = "queued"
        self.progress = 0
        self.message = "queued"
        self.log: list[str] = []
        self.created_at = time.time()

    def append(self, line: str) -> None:
        self.log.append(line)
        if len(self.log) > 260:
            self.log = self.log[-260:]

    def to_dict(self) -> dict[str, Any]:
        return {
            "job_id": self.job_id,
            "app_id": self.app_id,
            "status": self.status,
            "progress": self.progress,
            "message": self.message,
            "log": self.log,
        }


class StoreState:
    def __init__(self) -> None:
        self.jobs: dict[str, Job] = {}
        self.lock = threading.Lock()

    def create_job(self, app_id: str, weather: dict[str, Any] | None) -> Job:
        job = Job(f"job-{uuid.uuid4().hex[:12]}", app_id)
        with self.lock:
            self.jobs[job.job_id] = job
        thread = threading.Thread(target=self.run_install, args=(job, weather), daemon=True)
        thread.start()
        return job

    def get_job(self, job_id: str) -> Job | None:
        with self.lock:
            return self.jobs.get(job_id)

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
        status = run_transport_operation(run_sync, run_async, timeout=90.0)
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

    def send_bytes(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("content-type", content_type)
        self.send_header("content-length", str(len(body)))
        self.send_header("cache-control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, status: int, data: Any) -> None:
        self.send_bytes(status, json_bytes(data), "application/json; charset=utf-8")

    def send_error_json(self, status: int, message: str) -> None:
        self.send_json(status, {"error": message})

    def do_GET(self) -> None:
        try:
            path = request_path(self.path)
            if path == "/" or path == "/index.html":
                self.send_bytes(200, HTML.encode("utf-8"), "text/html; charset=utf-8")
            elif path == "/api/apps":
                self.send_json(200, {"apps": list_apps()})
            elif path in ("/api/ble/status", "/api/transport/status"):
                ok, output = run_status()
                self.send_json(200, {"ok": ok, "transport": TRANSPORT_KIND, "status": output})
            elif path == "/api/runtime/apps":
                ok, payload = run_runtime_apps()
                if ok:
                    self.send_json(200, {"ok": True, "transport": TRANSPORT_KIND, "runtime": payload})
                else:
                    self.send_json(200, {"ok": False, "transport": TRANSPORT_KIND, "error": payload})
            elif path == "/api/runtime/capabilities":
                ok, payload = run_runtime_capabilities()
                if ok:
                    self.send_json(200, {"ok": True, "transport": TRANSPORT_KIND, "capabilities": payload})
                else:
                    self.send_json(200, {"ok": False, "transport": TRANSPORT_KIND, "error": payload})
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
            if path.startswith("/api/apps/") and path.endswith("/install"):
                app_id = urllib.parse.unquote(path.split("/")[3])
                if app_id not in APP_ORDER:
                    self.send_error_json(404, "unknown app")
                    return
                payload = self.read_json()
                weather = payload.get("weather")
                if isinstance(weather, dict) and isinstance(weather.get("weather"), dict):
                    weather = weather["weather"]
                elif app_id == "weather_pet" and not weather:
                    weather_keys = {"city", "latitude", "longitude", "temperature_c", "humidity", "weather_code", "condition"}
                    if weather_keys.intersection(payload):
                        weather = payload
                job = STATE.create_job(app_id, weather if isinstance(weather, dict) else None)
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
