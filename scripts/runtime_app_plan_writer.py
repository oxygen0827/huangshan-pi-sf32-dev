#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import io
import json
import re
import shutil
import tempfile
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from runtime_package import (
    RuntimePackageError,
    package_errors_as_exceptions,
    safe_package_id,
    safe_package_path,
    validate_package,
)


ROOT_DIR = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_ROOT = ROOT_DIR / "generated" / "runtime_apps"
MAX_PLAN_FILE_BYTES = 512 * 1024
MAX_PLAN_TEXT_LEN = 512
MAX_PLAN_FILES = 24
RUNTIME_PROFILE = "huangshan-pi"


class RuntimeAppPlanError(ValueError):
    pass


@dataclass(frozen=True)
class RuntimeAppPlanResult:
    app_id: str
    files: dict[str, bytes]
    metadata: dict[str, Any]
    app_dir: Path | None = None
    archive_path: Path | None = None


def fail(message: str) -> None:
    raise RuntimeAppPlanError(message)


def require_object(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{label} must be an object")
    return value


def optional_string(value: Any, label: str, fallback: str = "", max_len: int = MAX_PLAN_TEXT_LEN) -> str:
    if value is None:
        return fallback
    if not isinstance(value, str):
        fail(f"{label} must be a string")
    text = value.strip()
    if len(text) > max_len:
        fail(f"{label} must be at most {max_len} characters")
    if any(ord(ch) < 32 and ch not in "\r\n\t" for ch in text):
        fail(f"{label} must not contain control characters")
    return text


def require_string(value: Any, label: str, max_len: int = MAX_PLAN_TEXT_LEN) -> str:
    text = optional_string(value, label, "", max_len)
    if not text:
        fail(f"{label} must be a non-empty string")
    return text


def as_string_list(value: Any, label: str, max_items: int = 12, max_len: int = 48) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list):
        fail(f"{label} must be a list of strings")
    if len(value) > max_items:
        fail(f"{label} supports at most {max_items} entries")
    result: list[str] = []
    for index, item in enumerate(value, start=1):
        text = require_string(item, f"{label}[{index}]", max_len=max_len)
        result.append(text)
    return result


def checked_app_id(value: str) -> str:
    try:
        with package_errors_as_exceptions():
            return safe_package_id(value)
    except RuntimePackageError as exc:
        raise RuntimeAppPlanError(str(exc)) from exc


def checked_path(value: str) -> str:
    try:
        with package_errors_as_exceptions():
            return safe_package_path(value)
    except RuntimePackageError as exc:
        raise RuntimeAppPlanError(str(exc)) from exc


def slugify_app_id(name: str) -> str:
    text = name.strip().lower()
    text = re.sub(r"[^a-z0-9]+", "_", text)
    text = re.sub(r"_+", "_", text).strip("_")
    if not text or not ("a" <= text[0] <= "z"):
        text = f"app_{text}" if text else "app"
    return checked_app_id(text[:15].rstrip("_") or "app")


def lua_quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\r", " ").replace("\n", " ") + '"'


def lua_color(value: Any, fallback: str) -> str:
    text = str(value or fallback).strip()
    if re.match(r"^#[0-9a-fA-F]{6}$", text):
        return "0x" + text[1:]
    if re.match(r"^0x[0-9a-fA-F]{6}$", text):
        return text
    return fallback


def build_default_lua(app_id: str, name: str, description: str, labels: list[dict[str, Any]]) -> bytes:
    lines = [
        "local root = lv_scr_act()",
        "lv_obj_clean(root)",
        "lv_obj_set_style_bg_color(root, 0x101820)",
        "lv_obj_set_style_text_color(root, 0xf8fafc)",
        f"vibe_label({lua_quote(name)}, 30, 44, 330, LV_ALIGN_TOP_LEFT, 0xf8fafc)",
    ]
    if description:
        lines.append(f"vibe_label({lua_quote(description[:64])}, 30, 92, 330, LV_ALIGN_TOP_LEFT, 0xcbd5e1)")

    y = 150
    for label in labels[:4]:
        text = optional_string(label.get("text") or label.get("label") or label.get("capability"), "label.text", "", 64)
        if not text:
            continue
        color = lua_color(label.get("color"), "0x93c5fd")
        lines.append(f"vibe_label({lua_quote(text)}, 30, {y}, 330, LV_ALIGN_TOP_LEFT, {color})")
        y += 42

    lines.append(f"print({lua_quote(f'[{app_id}] generated plan entry reached')})")
    return ("\n".join(lines) + "\n").encode("utf-8")


def normalize_components(value: Any) -> list[dict[str, Any]]:
    if value is None:
        return []
    if not isinstance(value, list):
        fail("components must be a list")
    if len(value) > 8:
        fail("components supports at most 8 entries")
    result: list[dict[str, Any]] = []
    for index, item in enumerate(value, start=1):
        result.append(dict(require_object(item, f"components[{index}]")))
    return result


def decode_plan_file(item: dict[str, Any], index: int) -> tuple[str, bytes]:
    path = checked_path(require_string(item.get("path"), f"files[{index}].path", max_len=96))
    encoding = optional_string(item.get("encoding") or item.get("type"), f"files[{index}].encoding", "text", 16).lower()
    if encoding in {"base64", "binary"}:
        content = require_string(item.get("content"), f"files[{index}].content", max_len=MAX_PLAN_FILE_BYTES)
        try:
            data = base64.b64decode(content, validate=True)
        except Exception as exc:
            raise RuntimeAppPlanError(f"files[{index}].content must be valid base64") from exc
    else:
        content = require_string(item.get("content"), f"files[{index}].content", max_len=MAX_PLAN_FILE_BYTES)
        data = content.encode("utf-8")
    if len(data) > MAX_PLAN_FILE_BYTES:
        fail(f"files[{index}] exceeds {MAX_PLAN_FILE_BYTES} bytes")
    return path, data


def plan_files(plan: dict[str, Any]) -> dict[str, bytes]:
    raw_files = plan.get("files") or []
    if not isinstance(raw_files, list):
        fail("files must be a list")
    if len(raw_files) > MAX_PLAN_FILES:
        fail(f"files supports at most {MAX_PLAN_FILES} entries")
    files: dict[str, bytes] = {}
    for index, item in enumerate(raw_files, start=1):
        path, data = decode_plan_file(require_object(item, f"files[{index}]"), index)
        if path == "manifest.json":
            continue
        if path in files:
            fail(f"duplicate file path: {path}")
        files[path] = data
    return files


def manifest_from_plan(app_id: str, app: dict[str, Any], plan: dict[str, Any], components: list[dict[str, Any]]) -> dict[str, Any]:
    name = require_string(app.get("name"), "app.name", 64)
    description = require_string(app.get("description"), "app.description", 160)
    manifest: dict[str, Any] = {
        "schemaVersion": 1,
        "kind": "huangshan-runtime-app-manifest",
        "id": app_id,
        "name": name,
        "description": description,
        "category": optional_string(app.get("category"), "app.category", "General", 32) or "General",
        "icon": optional_string(app.get("icon"), "app.icon", "app", 32) or "app",
        "author": optional_string(app.get("author"), "app.author", "App Plan Writer", 64) or "App Plan Writer",
        "screenshot": optional_string(app.get("screenshot"), "app.screenshot", f"generated:{app_id}", 96) or f"generated:{app_id}",
        "requirements": as_string_list(app.get("requirements"), "app.requirements", max_items=8, max_len=48) or ["Runtime"],
        "entry": "main.lua",
        "runtimeProfile": RUNTIME_PROFILE,
    }
    capabilities = as_string_list(app.get("capabilities") or plan.get("capabilities"), "app.capabilities", max_items=12, max_len=48)
    if capabilities:
        manifest["capabilities"] = capabilities
    if components:
        manifest["components"] = components
    return manifest


def build_app_plan_package(plan: dict[str, Any]) -> RuntimeAppPlanResult:
    plan = require_object(plan, "plan")
    app = require_object(plan.get("app"), "app")
    name = require_string(app.get("name"), "app.name", 64)
    app_id = checked_app_id(str(app.get("id") or app.get("appId") or app.get("packageId") or slugify_app_id(name)))
    description = require_string(app.get("description"), "app.description", 160)
    components = normalize_components(plan.get("components") if "components" in plan else app.get("components"))
    files = plan_files(plan)

    script = plan.get("mainLua", plan.get("lua", plan.get("script")))
    if script is not None:
        files["main.lua"] = (require_string(script, "mainLua", max_len=MAX_PLAN_FILE_BYTES).rstrip() + "\n").encode("utf-8")
    elif "main.lua" not in files:
        labels = [item for item in components if str(item.get("type") or "status") == "label"]
        files["main.lua"] = build_default_lua(app_id, name, description, labels)

    manifest = manifest_from_plan(app_id, app, plan, components)
    screenshot = str(manifest.get("screenshot") or "")
    if screenshot and not screenshot.startswith("generated:") and checked_path(screenshot) not in files:
        fail(f"manifest screenshot {screenshot!r} is missing from plan files")
    files["manifest.json"] = json.dumps(manifest, ensure_ascii=False, indent=2).encode("utf-8") + b"\n"
    if "README.md" not in files:
        files["README.md"] = (
            f"# {name}\n\n"
            f"{description}\n\n"
            f"- App ID: `{app_id}`\n"
            f"- Runtime profile: `{RUNTIME_PROFILE}`\n"
            f"- Generated by: `scripts/runtime_app_plan_writer.py`\n"
        ).encode("utf-8")

    try:
        with package_errors_as_exceptions():
            package_id, safe_files = validate_package(app_id, files)
    except RuntimePackageError as exc:
        raise RuntimeAppPlanError(f"generated app validation failed: {exc}") from exc

    metadata = {
        "id": package_id,
        "name": name,
        "description": description,
        "category": manifest.get("category"),
        "icon": manifest.get("icon"),
        "author": manifest.get("author"),
        "screenshot": manifest.get("screenshot"),
        "requirements": manifest.get("requirements"),
        "fileCount": len(safe_files),
        "byteCount": sum(len(data) for data in safe_files.values()),
    }
    return RuntimeAppPlanResult(package_id, safe_files, metadata)


def assert_inside(root: Path, target: Path, label: str) -> None:
    root = root.resolve()
    target = target.resolve()
    if target != root and root not in target.parents:
        fail(f"{label} must stay inside {root}")


def write_files(app_dir: Path, files: dict[str, bytes]) -> None:
    for rel, data in sorted(files.items()):
        path = app_dir / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def zip_package_bytes(files: dict[str, bytes]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        current = time.localtime()
        for path, data in sorted(files.items()):
            info = zipfile.ZipInfo(path, current[:6])
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
    return buffer.getvalue()


def write_app_plan_object(
    plan: dict[str, Any],
    output_root: Path = DEFAULT_OUTPUT_ROOT,
    package_output: bool = False,
) -> RuntimeAppPlanResult:
    result = build_app_plan_package(plan)
    output_root = output_root.resolve()
    app_dir = output_root / result.app_id
    assert_inside(output_root, app_dir, "appDir")
    shutil.rmtree(app_dir, ignore_errors=True)
    app_dir.mkdir(parents=True, exist_ok=True)
    archive_path: Path | None = None
    try:
        write_files(app_dir, result.files)
        if package_output:
            archive_path = output_root / f"{result.app_id}.happ"
            archive_path.parent.mkdir(parents=True, exist_ok=True)
            archive_path.write_bytes(zip_package_bytes(result.files))
        return RuntimeAppPlanResult(result.app_id, result.files, result.metadata, app_dir, archive_path)
    except Exception:
        shutil.rmtree(app_dir, ignore_errors=True)
        if archive_path:
            archive_path.unlink(missing_ok=True)
        raise


def write_app_plan(plan_path: Path, output_root: Path = DEFAULT_OUTPUT_ROOT, package_output: bool = False) -> RuntimeAppPlanResult:
    try:
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise RuntimeAppPlanError(f"invalid JSON plan {plan_path.name}: {exc}") from exc
    return write_app_plan_object(plan, output_root, package_output)


def sample_plan() -> dict[str, Any]:
    return {
        "app": {
            "id": "plan_demo",
            "name": "Plan Demo",
            "description": "Generated from a Huangshan Runtime app plan.",
            "category": "Tools",
            "icon": "sparkles",
            "author": "Runtime App Plan Writer",
            "requirements": ["Runtime", "Display"],
            "capabilities": ["display"],
        },
        "components": [
            {"type": "label", "text": "Generated safely", "color": "#5eead4"},
        ],
    }


def run_self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="runtime-app-plan-") as temp:
        root = Path(temp)
        result = write_app_plan_object(sample_plan(), root, package_output=True)
        if result.app_id != "plan_demo":
            fail(f"self-test app id mismatch: {result.app_id}")
        if not result.app_dir or not (result.app_dir / "main.lua").is_file():
            fail("self-test did not write main.lua")
        if not result.archive_path or not result.archive_path.is_file():
            fail("self-test did not write .happ archive")
        manifest = json.loads(result.files["manifest.json"].decode("utf-8"))
        if not isinstance(manifest.get("files"), list) or not manifest.get("integrity", {}).get("filesDigest"):
            fail("self-test manifest integrity was not generated")
        if any(item.get("path") == "manifest.json" for item in manifest["files"]):
            fail("self-test manifest must not hash manifest.json itself")

        bad = sample_plan()
        bad["app"] = dict(bad["app"], capabilities=["native"])
        try:
            build_app_plan_package(bad)
        except RuntimeAppPlanError as exc:
            if "BLE/serial" not in str(exc):
                fail(f"self-test wrong bad capability error: {exc}")
        else:
            fail("self-test bad capability unexpectedly passed")
    print("runtime_app_plan_writer self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a Huangshan Runtime app package from a JSON plan.")
    parser.add_argument("plan", nargs="?", type=Path, help="JSON plan file.")
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--package", action="store_true", help="Also write a .happ archive beside the app directory.")
    parser.add_argument("--self-test", action="store_true", help="Run offline checks and exit.")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return 0
    if not args.plan:
        parser.error("plan is required unless --self-test is used")

    result = write_app_plan(args.plan, args.output_root, args.package)
    print(f"generated {result.app_id}")
    if result.app_dir:
        print(f"app {result.app_dir}")
    if result.archive_path:
        print(f"package {result.archive_path}")
    print(f"files {len(result.files)} bytes {sum(len(data) for data in result.files.values())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
