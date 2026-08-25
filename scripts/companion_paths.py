#!/usr/bin/env python3
from __future__ import annotations

import os
import sys
from pathlib import Path


def companion_root() -> Path:
    configured = os.environ.get("VIBEBOARD_COMPANION_ROOT")
    if configured:
        return Path(configured).expanduser().resolve()
    bundled = getattr(sys, "_MEIPASS", None)
    if bundled:
        return Path(bundled).resolve()
    return Path(__file__).resolve().parents[1]
