#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import time
from dataclasses import dataclass

from runtime_install_serial import (
    codex_pet_preview_ready,
    codex_pet_preview_states,
    codex_pet_ticks_advanced,
    validate_codex_pet_ready,
)
from runtime_transport import (
    APP_MANAGER_API,
    POWER_API,
    RuntimeTransportError,
    SerialTransport,
    SerialTransportOptions,
)


@dataclass(frozen=True)
class ReloadResult:
    iteration: int
    elapsed_ms: int
    reloads: int
    ui_ticks: int


def _json_object(text: str, expected_api: str) -> dict[str, object]:
    value = json.loads(text)
    if not isinstance(value, dict) or value.get("api") != expected_api:
        raise RuntimeTransportError(f"unexpected {expected_api} response: {value!r}")
    return value


def _app_status(transport: SerialTransport, *, wait: float = 0.12) -> dict[str, object]:
    return _json_object(
        transport.read_json("vb_runtime_app", "app", APP_MANAGER_API, timeout=2.0, wait=wait),
        APP_MANAGER_API,
    )


def _wait_pet_ready(transport: SerialTransport, *, timeout: float) -> dict[str, object]:
    deadline = time.monotonic() + timeout
    previous: dict[str, object] | None = None
    last_error = "no Codex Pet status received"
    while time.monotonic() < deadline:
        try:
            latest = validate_codex_pet_ready(
                transport.read_json(
                    "vb_runtime_codex_pet_status",
                    "codex_pet",
                    wait=0.08,
                    timeout=1.5,
                )
            )
            if previous is not None and codex_pet_ticks_advanced(previous, latest):
                return latest
            previous = latest
            last_error = "Codex Pet UI ticks have not advanced yet"
        except RuntimeTransportError as exc:
            previous = None
            last_error = str(exc)
        time.sleep(0.25)
    raise RuntimeTransportError(f"Codex Pet did not become ready within {timeout:.1f}s: {last_error}")


def _prepare_idle_pet(transport: SerialTransport, iteration: int, *, timeout: float) -> None:
    # Do not send a flow command here.  Companion heartbeats share the same
    # UART log and can interleave its ACK with a diagnostic response.  A
    # moving, fully loaded pet is the stable baseline; state sweep is tested
    # separately through the dedicated CLI.
    del iteration
    _wait_pet_ready(transport, timeout=timeout)
    time.sleep(0.4)


def reload_once(
    transport: SerialTransport,
    iteration: int,
    *,
    timeout: float,
) -> ReloadResult:
    before_app = _app_status(transport)
    _prepare_idle_pet(transport, iteration, timeout=timeout)
    expected_reload = int(before_app.get("reloads", 0)) + 1
    failures = int(before_app.get("reload_failures", 0))
    timeouts = int(before_app.get("reload_timeouts", 0))
    started = time.monotonic()
    transport.command("vb_runtime_reload", wait=0.08, echo=False)
    deadline = started + timeout
    last: dict[str, object] = {}
    while time.monotonic() < deadline:
        last = _app_status(transport, wait=0.16)
        if int(last.get("reload_failures", -1)) != failures:
            raise RuntimeTransportError(f"reload {iteration} increased failure count: {last}")
        if int(last.get("reload_timeouts", -1)) != timeouts:
            raise RuntimeTransportError(f"reload {iteration} increased timeout count: {last}")
        if (
            int(last.get("reloads", 0)) >= expected_reload
            and last.get("state") == "running"
            and last.get("running") == 1
            and last.get("failed") == 0
            and last.get("pending_reload") == 0
            and last.get("reloading") == 0
            and last.get("reload_phase") == "idle"
        ):
            # Reload constructs a fresh pet UI, so uiTicks may restart at zero.
            # Require two advancing frames from the new instance instead of
            # comparing counters across different instances.
            latest_pet = _wait_pet_ready(transport, timeout=timeout)
            return ReloadResult(
                iteration=iteration,
                elapsed_ms=round((time.monotonic() - started) * 1000),
                reloads=int(last["reloads"]),
                ui_ticks=int(latest_pet["uiTicks"]),
            )
        time.sleep(0.3)
    raise RuntimeTransportError(f"reload {iteration} did not recover within {timeout:.1f}s: {last}")


def run_state_sweep(transport: SerialTransport, *, timeout: float) -> list[dict[str, object]]:
    sequence = int(time.time()) & 0xFFFFFFFF
    results: list[dict[str, object]] = []
    states = codex_pet_preview_states()
    try:
        for index, state in enumerate(states):
            transport.flow_send("pet.preview", sequence + index, state)
            deadline = time.monotonic() + timeout
            first_frame: int | None = None
            previous: dict[str, object] | None = None
            while time.monotonic() < deadline:
                latest = validate_codex_pet_ready(
                    transport.read_json(
                        "vb_runtime_codex_pet_status",
                        "codex_pet",
                        wait=0.08,
                        timeout=1.5,
                    )
                )
                if (
                    latest.get("assetState") == state
                    and latest.get("requestedAssetState") == state
                    and latest.get("loaderPhase") == 0
                ):
                    frame = latest.get("frame")
                    if first_frame is None and isinstance(frame, int) and not isinstance(frame, bool):
                        first_frame = frame
                    if (
                        previous is not None
                        and codex_pet_ticks_advanced(previous, latest)
                        and codex_pet_preview_ready(latest, state, first_frame)
                    ):
                        results.append(
                            {
                                "state": state,
                                "frame": latest.get("frame"),
                                "frames": latest.get("frames"),
                                "uiTicks": latest.get("uiTicks"),
                            }
                        )
                        break
                    previous = latest
                else:
                    first_frame = None
                    previous = None
                time.sleep(0.12)
            else:
                raise RuntimeTransportError(f"nine-state sweep timed out in state {state!r}")
    finally:
        transport.flow_send("pet.preview", sequence + len(states), "auto")
    return results


def run_click_test(transport: SerialTransport, *, timeout: float) -> dict[str, object]:
    sequence = (int(time.time()) + 100) & 0xFFFFFFFF
    transport.flow_send("pet.preview", sequence, "auto")
    before = validate_codex_pet_ready(transport.codex_pet())
    transport.flow_send("pet.preview", sequence + 1, "tap")
    deadline = time.monotonic() + timeout
    first_frame: int | None = None
    previous: dict[str, object] | None = None
    jumping: dict[str, object] | None = None
    while time.monotonic() < deadline:
        latest = validate_codex_pet_ready(
            transport.read_json(
                "vb_runtime_codex_pet_status",
                "codex_pet",
                wait=0.08,
                timeout=1.5,
            )
        )
        if latest.get("assetState") == "jumping" and latest.get("loaderPhase") == 0:
            frame = latest.get("frame")
            if first_frame is None and isinstance(frame, int) and not isinstance(frame, bool):
                first_frame = frame
            elif (
                previous is not None
                and codex_pet_ticks_advanced(previous, latest)
                and codex_pet_preview_ready(latest, "jumping", first_frame)
            ):
                jumping = latest
                break
            previous = latest
        time.sleep(0.12)
    if jumping is None:
        raise RuntimeTransportError("tap did not produce advancing jumping frames")
    transport.flow_send("pet.preview", sequence + 2, "tap")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        latest = validate_codex_pet_ready(transport.codex_pet())
        if (
            latest.get("assetState") != "jumping"
            and latest.get("requestedAssetState") != "jumping"
            and codex_pet_ticks_advanced(jumping, latest)
        ):
            return {
                "before": before.get("assetState"),
                "jumpFrame": jumping.get("frame"),
                "returned": latest.get("assetState"),
            }
        time.sleep(0.12)
    raise RuntimeTransportError("second tap did not leave the jumping state")


def run_self_test() -> None:
    value = _json_object('{"api":"vibeboard-huangshan-app-manager/v1"}', APP_MANAGER_API)
    assert value["api"] == APP_MANAGER_API
    try:
        _json_object("[]", APP_MANAGER_API)
    except RuntimeTransportError:
        pass
    else:
        raise AssertionError("non-object status should fail")
    assert len(codex_pet_preview_states()) == 9
    print("runtime_recovery_soak self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description="Exercise Runtime self-recovery on a real Huangshan Pi.")
    parser.add_argument("port", nargs="?", default="/dev/cu.usbserial-13220")
    parser.add_argument("--iterations", type=int, default=20)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--skip-state-sweep", action="store_true")
    parser.add_argument("--skip-click-test", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        run_self_test()
        return 0
    if args.iterations < 1:
        parser.error("--iterations must be at least 1")

    options = SerialTransportOptions(
        port=args.port,
        command_wait=0.12,
        final_wait=0.4,
        ready_timeout=30.0,
        echo=False,
    )
    with SerialTransport(options) as transport:
        results = [
            reload_once(transport, iteration, timeout=args.timeout)
            for iteration in range(1, args.iterations + 1)
        ]
        sweep = [] if args.skip_state_sweep else run_state_sweep(transport, timeout=args.timeout)
        click = {} if args.skip_click_test else run_click_test(transport, timeout=args.timeout)
        power = _json_object(transport.power(), POWER_API)
        final_app = _app_status(transport)

    payload = {
        "api": "vibeboard-runtime-recovery-soak/v1",
        "iterations": args.iterations,
        "reloads": [result.__dict__ for result in results],
        "maxReloadMs": max(result.elapsed_ms for result in results),
        "states": sweep,
        "click": click,
        "soc": power,
        "finalApp": final_app,
    }
    print(json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError, RuntimeTransportError) as exc:
        print(f"runtime recovery soak failed: {exc}")
        raise SystemExit(1)
