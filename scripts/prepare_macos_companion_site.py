#!/usr/bin/env python3
"""Generate the public Web page for one validated Mac Companion release."""
from __future__ import annotations

import argparse
import html
import os
import re
from pathlib import Path

from check_macos_companion_release import ReleaseCheckError, _https_url


DOWNLOAD_META = re.compile(
    r'(<meta\s+name="vibeboard-companion-download"\s+content=")[^"]*(">)',
    re.IGNORECASE,
)


def prepare_site(source: Path, destination: Path, download_url: str) -> None:
    download_url = _https_url(download_url, "Companion download URL")
    source_text = source.read_text(encoding="utf-8")
    rendered, count = DOWNLOAD_META.subn(
        lambda match: f"{match.group(1)}{html.escape(download_url, quote=True)}{match.group(2)}",
        source_text,
    )
    if count != 1:
        raise ReleaseCheckError("the Web source must contain exactly one Companion download meta tag")

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.tmp-{os.getpid()}")
    temporary.write_text(rendered, encoding="utf-8")
    os.replace(temporary, destination)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--download-url", required=True)
    args = parser.parse_args()
    try:
        prepare_site(args.source, args.destination, args.download_url)
    except (OSError, ReleaseCheckError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
