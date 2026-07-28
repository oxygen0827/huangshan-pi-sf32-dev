#!/usr/bin/env python3
from __future__ import annotations

import sys

import codex_pet_bridge
import codex_pet_hook


def run_agent_self_test() -> None:
    import importlib

    import CoreBluetooth  # noqa: F401
    import Foundation  # noqa: F401
    import objc  # noqa: F401
    importlib.import_module("bleak.backends.corebluetooth.client")

    from companion_paths import companion_root
    from codex_pet_companion import run_self_test as companion_self_test
    from hpet_package import run_self_test as hpet_self_test

    root = companion_root()
    required = (
        root / "scripts" / "codex_pet_web.html",
        root / "scripts" / "codex_pet_hook.py",
        root / "scripts" / "petdex_state_contract.json",
        root / "scripts" / "runtime_apps" / "codex_pet" / "manifest.json",
    )
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError(f"Companion resources are missing: {', '.join(missing)}")
    codex_pet_hook.self_test()
    companion_self_test()
    hpet_self_test()
    print("codex_pet_agent self-test ok")


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--agent-self-test":
        run_agent_self_test()
        return 0
    if len(sys.argv) > 1 and sys.argv[1] == "--firmware-release":
        import firmware_release
        return firmware_release.main(sys.argv[2:])
    if len(sys.argv) > 1 and sys.argv[1] == "--firmware-flash":
        import flash
        return flash.main(sys.argv[2:])
    if len(sys.argv) > 1 and sys.argv[1] == "--firmware-health":
        import runtime_install_serial
        return runtime_install_serial.main(sys.argv[2:])
    if len(sys.argv) > 1 and sys.argv[1] == "--hook":
        del sys.argv[1]
        if len(sys.argv) > 1 and sys.argv[1].endswith("codex_pet_hook.py"):
            del sys.argv[1]
        return codex_pet_hook.main()
    return codex_pet_bridge.main()


if __name__ == "__main__":
    raise SystemExit(main())
