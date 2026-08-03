#!/usr/bin/env python3
"""Offline regression checks for the Mac Companion release gate."""
from __future__ import annotations

import hashlib
import json
import tempfile
from pathlib import Path

from check_macos_companion_release import ReleaseCheckError, _https_url, check_release
from prepare_macos_companion_site import prepare_site


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="macos-release-test-") as temp:
        root = Path(temp)
        dmg = root / "Companion.dmg"
        checksum = root / "Companion.dmg.sha256"
        manifest = root / "update-manifest.json"
        page = root / "index.html"
        dmg.write_bytes(b"signed-dmg-fixture")
        digest = hashlib.sha256(dmg.read_bytes()).hexdigest()
        url = "https://ldcx.tech/static/codex-pet/companion/Companion.dmg"
        checksum.write_text(f"{digest}  {dmg.name}\n", encoding="utf-8")
        manifest.write_text(json.dumps({"version": "1.0.0", "build": "1", "downloadURL": url, "sha256": digest}), encoding="utf-8")
        source_page = root / "source.html"
        source_page.write_text('<meta name="vibeboard-companion-download" content="">', encoding="utf-8")
        prepare_site(source_page, page, url)
        result = check_release(dmg, checksum, manifest, page, url, "1.0.0", "1")
        assert result["sha256"] == digest
        page.write_text('<meta name="vibeboard-companion-download" content="https://example.com/wrong.dmg">', encoding="utf-8")
        try:
            check_release(dmg, checksum, manifest, page, url, "1.0.0", "1")
        except ReleaseCheckError:
            pass
        else:
            raise AssertionError("a page pointing at the wrong DMG passed the release gate")
        prepare_site(source_page, page, url)
        try:
            check_release(dmg, checksum, manifest, page, url, "1.0.0", "2")
        except ReleaseCheckError:
            pass
        else:
            raise AssertionError("a stale update manifest passed the release gate")
        try:
            _https_url("https://ldcx.tech:444/Companion.dmg", "test URL")
        except ReleaseCheckError:
            pass
        else:
            raise AssertionError("a non-standard HTTPS port passed the release gate")
    print("macos_companion_release self-test ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
