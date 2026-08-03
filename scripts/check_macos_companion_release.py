#!/usr/bin/env python3
"""Validate the public Mac Companion release contract before upload."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit


class ReleaseCheckError(RuntimeError):
    pass


class _MetaParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.download_url = ""

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag.lower() != "meta":
            return
        values = {key.lower(): value or "" for key, value in attrs}
        if values.get("name") == "vibeboard-companion-download":
            self.download_url = values.get("content", "")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(128 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _https_url(value: object, label: str) -> str:
    text = str(value or "")
    parsed = urlsplit(text)
    try:
        port = parsed.port
    except ValueError as exc:
        raise ReleaseCheckError(f"{label} must be a credential-free https URL") from exc
    if (
        parsed.scheme != "https"
        or not parsed.netloc
        or parsed.username
        or parsed.password
        or parsed.fragment
        or port not in (None, 443)
    ):
        raise ReleaseCheckError(f"{label} must be a credential-free https URL")
    return text


def check_release(
    dmg: Path,
    checksum: Path,
    manifest: Path,
    site_html: Path,
    expected_url: str,
    expected_version: str | None = None,
    expected_build: str | None = None,
) -> dict[str, object]:
    for path in (dmg, checksum, manifest, site_html):
        if not path.is_file():
            raise ReleaseCheckError(f"missing release file: {path}")
    if dmg.stat().st_size <= 0:
        raise ReleaseCheckError("DMG is empty")

    digest = _sha256(dmg)
    checksum_text = checksum.read_text(encoding="utf-8").strip()
    if re.fullmatch(r"[0-9a-f]{64}(?:\s+\S+)?", checksum_text) is None:
        raise ReleaseCheckError("DMG checksum file is not a SHA-256 record")
    if checksum_text.split()[0] != digest:
        raise ReleaseCheckError("DMG checksum file does not match the DMG")

    try:
        value = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ReleaseCheckError(f"invalid update manifest: {exc}") from exc
    if not isinstance(value, dict) or not isinstance(value.get("version"), str) or not isinstance(value.get("build"), str):
        raise ReleaseCheckError("update manifest is missing version/build")
    version = value["version"]
    build = value["build"]
    if re.fullmatch(r"\d+\.\d+\.\d+", version) is None or re.fullmatch(r"\d+", build) is None:
        raise ReleaseCheckError("update manifest version/build format is invalid")
    if expected_version is not None and version != expected_version:
        raise ReleaseCheckError("update manifest version does not match the release version")
    if expected_build is not None and build != expected_build:
        raise ReleaseCheckError("update manifest build does not match the release build")
    download_url = _https_url(value.get("downloadURL"), "update manifest downloadURL")
    expected_url = _https_url(expected_url, "expected download URL")
    if download_url != expected_url:
        raise ReleaseCheckError("update manifest downloadURL does not match the release URL")
    if value.get("sha256") != digest:
        raise ReleaseCheckError("update manifest sha256 does not match the DMG")

    parser = _MetaParser()
    parser.feed(site_html.read_text(encoding="utf-8"))
    if parser.download_url != expected_url:
        raise ReleaseCheckError("public Web page does not point at this Companion release")
    return {
        "version": version,
        "build": build,
        "downloadURL": expected_url,
        "sha256": digest,
        "dmgBytes": dmg.stat().st_size,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dmg", type=Path, required=True)
    parser.add_argument("--checksum", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--site-html", type=Path, required=True)
    parser.add_argument("--expected-url", required=True)
    parser.add_argument("--expected-version")
    parser.add_argument("--expected-build")
    args = parser.parse_args()
    try:
        print(json.dumps(check_release(
            args.dmg,
            args.checksum,
            args.manifest,
            args.site_html,
            args.expected_url,
            args.expected_version,
            args.expected_build,
        ), sort_keys=True))
    except ReleaseCheckError as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
