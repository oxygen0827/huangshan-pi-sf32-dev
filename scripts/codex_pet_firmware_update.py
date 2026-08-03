#!/usr/bin/env python3
"""Codex Pet Companion firmware-check presentation model.

This module deliberately owns only Companion-facing release discovery.  It does
not open a BLE connection, flash the board, or extend the Huangshan Runtime
protocol.  Firmware transport remains behind the signed FirmwareManager.
"""
from __future__ import annotations

import re
from typing import Any, Mapping, Protocol

from companion_firmware import FirmwareManagerError


SAFE_VERSION = re.compile(r"^[0-9][0-9A-Za-z.+_-]{0,63}$")


class FirmwareSource(Protocol):
    def status(self) -> dict[str, object]: ...

    def available(self) -> dict[str, object]: ...


def _safe_version(value: object) -> str | None:
    if not isinstance(value, str):
        return None
    candidate = value.strip()
    return candidate if SAFE_VERSION.fullmatch(candidate) else None


def _version_key(value: str) -> tuple[int, ...]:
    parts = []
    for token in value.split("."):
        digits = "".join(char for char in token if char.isdigit())
        parts.append(int(digits or 0))
    return tuple(parts or [0])


def board_firmware_version(capabilities: Mapping[str, object]) -> str | None:
    """Read an optional future firmware version without requiring it today."""
    for key in ("firmwareVersion", "firmware_version", "fw"):
        version = _safe_version(capabilities.get(key))
        if version:
            return version
    firmware = capabilities.get("firmware")
    if isinstance(firmware, Mapping):
        return _safe_version(firmware.get("version"))
    return None


def _result(
    *,
    state: str,
    message: str,
    firmware_status: Mapping[str, object],
    board_connected: bool,
    current: str | None = None,
    latest: str | None = None,
    current_source: str = "unknown",
    baseline: str | None = None,
) -> dict[str, object]:
    update_mode = str(firmware_status.get("updateMode") or "unavailable")
    wireless_dfu = firmware_status.get("wirelessDfu") is True
    usb = firmware_status.get("usbRecovery")
    usb_ready = isinstance(usb, Mapping) and usb.get("ready") is True
    return {
        "schemaVersion": 1,
        "state": state,
        "message": message,
        "boardConnected": board_connected,
        "current": current,
        "currentSource": current_source,
        "latest": latest,
        "baseline": baseline,
        "delivery": {
            "executor": "companion",
            "updateMode": update_mode,
            "wirelessDfu": wireless_dfu,
        },
        # The current signed manager is intentionally USB-only.  Do not turn a
        # release-discovery result into a false claim of Bluetooth DFU.
        "canInstall": state == "update_available" and update_mode == "verified_usb_recovery" and not wireless_dfu,
        "canInstallBaseline": (
            state == "current_version_unknown"
            and baseline is not None
            and usb_ready
            and update_mode == "verified_usb_recovery"
            and not wireless_dfu
        ),
        "usbRecovery": dict(usb) if isinstance(usb, Mapping) else {"ready": False},
    }


def check_for_firmware_update(
    source: FirmwareSource,
    *,
    board_connected: bool,
    runtime_capabilities: Mapping[str, object],
) -> dict[str, object]:
    """Return the user-facing state for one signed-release check."""
    status = source.status()
    if status.get("configured") is not True:
        return _result(
            state="not_configured",
            message="尚未配置官方签名固件发布源。",
            firmware_status=status,
            board_connected=board_connected,
        )

    try:
        available = source.available()
    except FirmwareManagerError as exc:
        return _result(
            state="unavailable",
            message=f"无法检查官方固件更新：{str(exc)[:160]}",
            firmware_status=status,
            board_connected=board_connected,
        )

    latest_row = available.get("latest")
    latest = _safe_version(latest_row.get("version")) if isinstance(latest_row, Mapping) else None
    baseline_row = available.get("baseline")
    baseline = _safe_version(baseline_row.get("version")) if isinstance(baseline_row, Mapping) else None
    if not latest:
        return _result(
            state="no_release",
            message="官方发布源中暂时没有可用的签名固件。",
            firmware_status=status,
            board_connected=board_connected,
        )
    if not board_connected:
        return _result(
            state="board_disconnected",
            message=f"已找到最新固件 {latest}。请先连接 VibeBoard 后再检查。",
            firmware_status=status,
            board_connected=False,
            latest=latest,
        )

    current = board_firmware_version(runtime_capabilities)
    if not current:
        usb = status.get("usbRecovery")
        usb_ready = isinstance(usb, Mapping) and usb.get("ready") is True
        detail = (
            f"已找到最新固件 {latest}，但当前板子未上报固件版本，"
            "无法安全判断是否需要升级。"
        )
        if baseline and usb_ready:
            detail += f" 已识别 USB 串口，可安装签名基础固件 {baseline}。"
        elif baseline:
            detail += " 请通过 USB 数据线连接板子后安装签名基础固件。"
        return _result(
            state="current_version_unknown",
            message=detail,
            firmware_status=status,
            board_connected=True,
            latest=latest,
            baseline=baseline,
        )

    comparison = _version_key(current)
    target = _version_key(latest)
    if comparison < target:
        return _result(
            state="update_available",
            message=(
                f"发现新固件 {latest}（当前 {current}）。"
                "升级由 Companion 执行，并在完成签名校验后开始。"
            ),
            firmware_status=status,
            board_connected=True,
            current=current,
            latest=latest,
            current_source="board_capabilities",
        )
    if comparison == target:
        return _result(
            state="up_to_date",
            message=f"当前固件 {current} 已是最新版本。",
            firmware_status=status,
            board_connected=True,
            current=current,
            latest=latest,
            current_source="board_capabilities",
        )
    return _result(
        state="board_version_newer",
        message=f"当前固件 {current} 高于发布源最新版本 {latest}，不会自动降级。",
        firmware_status=status,
        board_connected=True,
        current=current,
        latest=latest,
        current_source="board_capabilities",
    )


def run_self_test() -> None:
    class FakeFirmwareSource:
        def __init__(self, *, configured: bool = True, latest: str | None = "1.2.0", usb_ready: bool = True) -> None:
            self.configured = configured
            self.latest = latest
            self.usb_ready = usb_ready

        def status(self) -> dict[str, object]:
            return {
                "configured": self.configured,
                "updateMode": "verified_usb_recovery",
                "wirelessDfu": False,
                "usbRecovery": {"ready": self.usb_ready, "port": "/dev/cu.usbserial-test"},
            }

        def available(self) -> dict[str, object]:
            rows = [] if not self.latest else [{"version": self.latest, "url": "https://downloads.example.com/release.zip"}]
            baseline = {"version": "1.0.0", "baseline": True} if rows else None
            return {"latest": rows[0] if rows else None, "baseline": baseline, "releases": rows}

    source = FakeFirmwareSource()
    assert check_for_firmware_update(source, board_connected=False, runtime_capabilities={})["state"] == "board_disconnected"
    unknown = check_for_firmware_update(source, board_connected=True, runtime_capabilities={})
    assert unknown["state"] == "current_version_unknown" and unknown["canInstall"] is False
    assert unknown["baseline"] == "1.0.0" and unknown["canInstallBaseline"] is True
    assert check_for_firmware_update(FakeFirmwareSource(usb_ready=False), board_connected=True, runtime_capabilities={})["canInstallBaseline"] is False
    available = check_for_firmware_update(source, board_connected=True, runtime_capabilities={"fw": "1.1.0"})
    assert available["state"] == "update_available" and available["canInstall"] is True
    assert check_for_firmware_update(source, board_connected=True, runtime_capabilities={"firmware": {"version": "1.2.0"}})["state"] == "up_to_date"
    assert check_for_firmware_update(source, board_connected=True, runtime_capabilities={"firmwareVersion": "1.3.0"})["state"] == "board_version_newer"
    assert check_for_firmware_update(FakeFirmwareSource(configured=False), board_connected=True, runtime_capabilities={})["state"] == "not_configured"
    assert check_for_firmware_update(FakeFirmwareSource(latest=None), board_connected=True, runtime_capabilities={})["state"] == "no_release"
    assert board_firmware_version({"fw": "unsafe value"}) is None
    print("codex_pet_firmware_update self-test ok")


if __name__ == "__main__":
    run_self_test()
