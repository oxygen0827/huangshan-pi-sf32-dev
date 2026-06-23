#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


ROOT_DIR = Path(__file__).resolve().parents[1]
RUNTIME_APPS_DIR = ROOT_DIR / "scripts" / "runtime_apps"
BLE_INSTALL = ROOT_DIR / "scripts" / "runtime_install_ble.sh"
DEFAULT_DEVICE_NAME = "VibeBoard"
APP_ORDER = ["weather_pet", "auto_snake", "sensor_stage"]


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
      h1 { font-size: 28px; }
    }
  </style>
</head>
<body>
  <main>
    <header>
      <div>
        <h1>VibeBoard App Store</h1>
        <div class="subtitle">Mac mini 蓝牙 OTA 到黄山派 Runtime</div>
      </div>
      <div class="statusbar"><span id="bleDot" class="dot"></span><span id="bleText">检查蓝牙中...</span></div>
    </header>

    <section class="topgrid">
      <div class="panel">
        <h2>天气数据</h2>
        <div class="weather-row">
          <div>
            <label for="cityInput">城市兜底</label>
            <input id="cityInput" value="Shanghai" autocomplete="address-level2">
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
      </div>
    </section>

    <section id="apps" class="apps"></section>
    <section id="log" class="log">ready
</section>
  </main>

  <script>
    const state = { apps: [], weather: null, installing: false };
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
    const setBle = (ok, text) => {
      $("bleDot").className = `dot ${ok ? "ok" : "bad"}`;
      $("bleText").textContent = text;
    };
    const refreshBle = async () => {
      try {
        const data = await api("/api/ble/status");
        setBle(data.ok, data.status || "已连接");
      } catch (err) {
        setBle(false, err.message);
      }
    };
    const conditionLabel = item => `${item.city || "Current"} ${Math.round(item.temperature_c)}C ${item.condition} RH ${Math.round(item.humidity)}%`;
    const fetchWeather = async () => {
      $("weatherBtn").disabled = true;
      $("weatherHint").textContent = "获取天气中...";
      try {
        let payload = { city: $("cityInput").value.trim() || "Shanghai" };
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
          <button data-install="${esc(app.id)}" ${state.installing ? "disabled" : ""}>蓝牙 OTA 安装</button>
        </article>
      `).join("");
      document.querySelectorAll("[data-install]").forEach(button => {
        button.addEventListener("click", () => installApp(button.dataset.install));
      });
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
        await refreshBle();
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
      await refreshBle();
      fetchWeather();
      setInterval(refreshBle, 8000);
      $("weatherBtn").addEventListener("click", fetchWeather);
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


def resolve_city(city: str) -> tuple[float, float, str]:
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
    city = str(payload.get("city") or "Shanghai").strip() or "Shanghai"
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
    temp_dir = Path(tempfile.mkdtemp(prefix="vibeboard_weather_"))
    src = RUNTIME_APPS_DIR / "weather_pet"
    dst = temp_dir / "weather_pet"
    shutil.copytree(src, dst)
    city = str(weather.get("city") or "Current Location")[:80]
    condition = str(weather.get("condition") or "cloudy")[:24]
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
    return dst


def run_status(timeout: float = 18.0) -> tuple[bool, str]:
    cmd = [str(BLE_INSTALL), "--status-only", "--scan-timeout", "8", "--connect-timeout", "8"]
    proc = subprocess.run(cmd, cwd=ROOT_DIR, text=True, capture_output=True, timeout=timeout)
    output = (proc.stdout + proc.stderr).strip()
    return proc.returncode == 0, output


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
        job = Job(f"job-{int(time.time() * 1000)}", app_id)
        with self.lock:
            self.jobs[job.job_id] = job
        thread = threading.Thread(target=self.run_install, args=(job, weather), daemon=True)
        thread.start()
        return job

    def get_job(self, job_id: str) -> Job | None:
        with self.lock:
            return self.jobs.get(job_id)

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

            cmd = [
                str(BLE_INSTALL),
                "--package-dir",
                str(package_dir),
                "--app-id",
                job.app_id,
                "--scan-timeout",
                "10",
                "--connect-timeout",
                "10",
            ]
            job.progress = 18
            job.message = "connecting BLE"
            job.append("$ " + " ".join(cmd))
            proc = subprocess.Popen(
                cmd,
                cwd=ROOT_DIR,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
            if proc.stdout is None:
                raise RuntimeError("BLE install output pipe unavailable")
            for line in proc.stdout:
                line = line.rstrip()
                if line:
                    job.append(line)
                if "connected" in line:
                    job.progress = max(job.progress, 28)
                    job.message = "connected"
                elif "install_begin" in line:
                    job.progress = max(job.progress, 40)
                    job.message = "uploading"
                elif "install_file" in line:
                    job.progress = min(88, job.progress + 1)
                    job.message = "uploading files"
                elif "install_end" in line:
                    job.progress = max(job.progress, 92)
                    job.message = "finalizing"
                elif f"active={job.app_id}" in line:
                    job.progress = 98
                    job.message = "confirmed active"
            rc = proc.wait()
            if rc != 0:
                raise RuntimeError(f"BLE install failed with rc={rc}")
            job.status = "done"
            job.progress = 100
            job.message = f"installed {job.app_id}"
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
            if self.path == "/" or self.path == "/index.html":
                self.send_bytes(200, HTML.encode("utf-8"), "text/html; charset=utf-8")
            elif self.path == "/api/apps":
                self.send_json(200, {"apps": list_apps()})
            elif self.path == "/api/ble/status":
                ok, output = run_status()
                self.send_json(200, {"ok": ok, "status": output})
            elif self.path.startswith("/api/jobs/"):
                job_id = urllib.parse.unquote(self.path.rsplit("/", 1)[-1])
                job = STATE.get_job(job_id)
                if not job:
                    self.send_error_json(404, "job not found")
                    return
                self.send_json(200, job.to_dict())
            else:
                self.send_error_json(404, "not found")
        except subprocess.TimeoutExpired:
            self.send_error_json(504, "BLE status timed out")
        except Exception as exc:
            self.send_error_json(500, str(exc))

    def do_POST(self) -> None:
        try:
            if self.path == "/api/weather/resolve":
                weather = resolve_weather(self.read_json())
                self.send_json(200, {"weather": weather})
                return
            if self.path.startswith("/api/apps/") and self.path.endswith("/install"):
                app_id = urllib.parse.unquote(self.path.split("/")[3])
                if app_id not in APP_ORDER:
                    self.send_error_json(404, "unknown app")
                    return
                payload = self.read_json()
                job = STATE.create_job(app_id, payload.get("weather"))
                self.send_json(202, {"job_id": job.job_id})
                return
            self.send_error_json(404, "not found")
        except urllib.error.URLError as exc:
            self.send_error_json(502, f"weather network failed: {exc}")
        except Exception as exc:
            self.send_error_json(400, str(exc))


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the local VibeBoard App Store.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()

    try:
        server = ThreadingHTTPServer((args.host, args.port), Handler)
    except OSError:
        if args.port != 0:
            server = ThreadingHTTPServer((args.host, 0), Handler)
        else:
            raise
    url = f"http://{args.host}:{server.server_port}"
    print(f"VibeBoard App Store: {url}")
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
