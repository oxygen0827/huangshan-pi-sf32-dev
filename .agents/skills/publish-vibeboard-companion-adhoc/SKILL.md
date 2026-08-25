---
name: publish-vibeboard-companion-adhoc
description: Build, upload, and verify an ad-hoc signed macOS VibeBoard Companion release on ldcx.tech without Developer ID signing or Apple notarization. Use when the user asks to publish, upload, or update the latest local Companion, its public /pet/ page, cloud download source, or update manifest and explicitly accepts the existing non-notarized distribution flow.
---

# Publish VibeBoard Companion Ad Hoc

Publish the current workspace as one matched release: self-contained DMG, SHA-256 file, update manifest, and public Companion page. Keep the live site recoverable throughout.

## Safety Boundaries

- Require an explicit user request before changing the live server.
- Treat the working tree as the release input. Never clean, reset, stash, or revert unrelated changes.
- Never store, print, copy, or inspect a password, private key, token, or credential-file contents.
- Authenticate with the dedicated deployment key at `~/.ssh/ldcx_vibeboard_deploy`. Require `BatchMode=yes` and `IdentitiesOnly=yes` for every `ssh` and `scp` operation so publishing cannot fall back to the shared root password.
- If key authentication fails, stop. Ask the user to load the key with `ssh-add --apple-use-keychain ~/.ssh/ldcx_vibeboard_deploy`; never recover by reading a plaintext credential file or prompting for the root password.
- Do not place secrets in commands, repository files, shell history, logs, or final responses.
- Restrict root operations to these paths:
  - Companion artifacts: `/home/lincaigui/nginx/static/codex-pet/companion`
  - Public page: `/home/lincaigui/nginx/static/pet/index.html`
- Never open an unrestricted interactive root shell. Execute narrow, fixed remote commands with explicit targets.
- Never change the root password, SSH configuration, nginx configuration, firmware files, or another project.
- Never delete old versioned DMGs during a release. Back up the live manifest and page before promotion.
- Do not invoke `release_codex_pet_companion_macos.command`; it intentionally requires Developer ID and notarization.
- State clearly in the handoff that the artifact is ad-hoc signed and not Apple-notarized.

## Production Contract

Use these defaults unless the user explicitly changes the production environment:

```text
Public page: https://ldcx.tech/pet/
Download root: https://ldcx.tech/static/codex-pet/companion
Update manifest: https://ldcx.tech/static/codex-pet/companion/update-manifest.json
Firmware manifest: https://ldcx.tech/static/codex-pet/firmware/releases.json
Server: root@47.102.197.71
SSH identity: ~/.ssh/ldcx_vibeboard_deploy
Architecture: arm64
```

Read the live `update-manifest.json` before choosing a version. Never overwrite an existing version/build. Increment the build for every publication; use a minor version bump for a feature release and a patch bump for a fix release unless the user specifies otherwise.

## 1. Inspect And Test

Confirm the repository root, dirty worktree, current architecture, existing local artifacts, server directories, available space, and live manifest. Keep this inspection read-only.

Before any remote inspection, prove non-interactive key authentication succeeds:

```sh
ssh \
  -o BatchMode=yes \
  -o IdentitiesOnly=yes \
  -i ~/.ssh/ldcx_vibeboard_deploy \
  root@47.102.197.71 \
  'printf "key-login-ok\\n"'
```

Require the exact output `key-login-ok`. Do not continue if SSH asks for the server password or the command fails. Apply the same three SSH options to every later `ssh` call.

Run focused source checks before packaging:

```sh
node scripts/codex_pet_web_test.js
python3 scripts/codex_pet_progress.py --self-test
python3 -m py_compile scripts/codex_pet_progress.py scripts/codex_pet_companion.py
python3 scripts/macos_companion_release_test.py
git diff --check
```

Use `.venv/bin/python` where the repository virtual environment is required. If a test needs a temporary loopback listener, rerun that test with the necessary local permission instead of weakening it.

## 2. Build The Ad-Hoc Release

Set `release_version`, `release_build`, and `release_arch` from the chosen release. Build with the production URLs embedded and `CODEX_PET_CODESIGN_IDENTITY=-`:

```sh
env \
  VIBEBOARD_APP_VERSION="$release_version" \
  VIBEBOARD_BUILD_NUMBER="$release_build" \
  VIBEBOARD_PUBLIC_SITE_URL="https://ldcx.tech/pet/" \
  VIBEBOARD_UPDATE_MANIFEST_URL="https://ldcx.tech/static/codex-pet/companion/update-manifest.json" \
  VIBEBOARD_RELEASE_DOWNLOAD_URL="https://ldcx.tech/static/codex-pet/companion/VibeBoard-Companion-$release_version-macOS-$release_arch.dmg" \
  VIBEBOARD_FIRMWARE_MANIFEST_URL="https://ldcx.tech/static/codex-pet/firmware/releases.json" \
  VIBEBOARD_FIRMWARE_PUBLIC_KEY_PATH="$PWD/codex-pet-companion/keys/firmware-public.pem" \
  VIBEBOARD_SFTOOL_PATH="<verified-sftool-path>" \
  CODEX_PET_CODESIGN_IDENTITY=- \
  ./scripts/build_codex_pet_companion_app.command
```

Do not set `CODEX_PET_NOTARY_PROFILE`. The expected build message must identify an ad-hoc signed development build.

Generate the public page using the exact DMG URL:

```sh
.venv/bin/python scripts/prepare_macos_companion_site.py \
  --source scripts/codex_pet_web.html \
  --destination .local/dist/codex_pet_web.html \
  --download-url "$download_url"
```

## 3. Validate Before Upload

Require every check below to pass:

```sh
./scripts/verify_codex_pet_companion_app.sh '.local/VibeBoard Companion.app'
hdiutil verify "$dmg_path"
.venv/bin/python scripts/check_macos_companion_release.py \
  --dmg "$dmg_path" \
  --checksum "$dmg_path.sha256" \
  --manifest .local/dist/update-manifest.json \
  --site-html .local/dist/codex_pet_web.html \
  --expected-url "$download_url" \
  --expected-version "$release_version" \
  --expected-build "$release_build"
```

Record the exact DMG byte size and SHA-256 reported by the release checker. Stop before upload if any check fails.

## 4. Stage On The Server

Create a unique UTC stamp such as `20260804T010000Z`. Use a staging directory named `.release-$stamp` inside the Companion artifact directory. Check that the directory does not already exist, confirm available disk space, create it with mode `0700`, and do not touch live files yet.

Upload exactly these four files to staging:

```text
VibeBoard-Companion-<version>-macOS-<arch>.dmg
VibeBoard-Companion-<version>-macOS-<arch>.dmg.sha256
update-manifest.json
codex_pet_web.html
```

Use one bounded `scp` transfer with `-o BatchMode=yes -o IdentitiesOnly=yes -i ~/.ssh/ldcx_vibeboard_deploy`. Do not upload source files, credential files, or the local state directory.

## 5. Verify And Promote

Before promotion, use one narrow remote command to verify:

- all four staged files are regular files;
- DMG byte size equals the local release-check result;
- `sha256sum` equals both the uploaded checksum and the recorded local digest;
- manifest version, build, URL, and digest are exact;
- public HTML contains the exact versioned DMG URL and a marker unique to the new UI;
- the versioned DMG target does not already exist.

Promote in this order:

1. Copy the current `update-manifest.json` to `update-manifest.json.backup-$stamp`.
2. Copy the current public page to `index.html.backup-$stamp`.
3. Move the versioned DMG and checksum from staging into the artifact directory.
4. Move the manifest into place.
5. Move the staged HTML to `index.html.stage-$stamp`, set mode `0644`, then rename it to `index.html`.
6. Remove the now-empty staging directory.

Use same-filesystem `mv` for promotion. Preserve the previous versioned DMG. If validation fails before promotion, leave live files untouched and report the staging path.

## 6. Verify Public Delivery

Verify through the public HTTPS origin, not only the server filesystem:

- `https://ldcx.tech/pet/` renders the new UI.
- Every Companion download link targets the new versioned DMG.
- The public manifest reports the expected version, build, URL, and digest.
- A HEAD request to the public DMG returns `200 OK`, the expected content type, byte length, and range support.
- The server-side DMG SHA-256 matches the local digest.
- The page has no horizontal overflow or page-owned browser errors at desktop width.

If post-promotion verification fails, restore the timestamped manifest and page backups immediately, verify the restored public endpoints, and report the rollback. The new versioned DMG may remain because nothing points to it.

## 7. Report

Report the public page, version, build, DMG URL, byte size, SHA-256, validation results, and backup names. Do not include credentials. Remind the user that an ad-hoc, non-notarized DMG may trigger macOS Gatekeeper warnings.
