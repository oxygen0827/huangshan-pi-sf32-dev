# Repository Agent Instructions

## Huangshan Pi UI work

Before creating, modifying, reviewing, or debugging any board-side screen UI,
read and follow `.agents/skills/huangshan-screen-ui/SKILL.md`. This applies to
LVGL C code, Runtime Lua apps, manifests that generate UI, games, dashboards,
launchers, and display screenshots.

Treat the physical 390x450 rounded AMOLED safe area as a product constraint,
not an optional visual refinement. Do not consider a UI task complete until the
skill's layout audit and target review steps have been addressed.

## Huangshan Pi sensor work

Before developing or modifying an App, identify its requested hardware
capabilities from the behavior and manifest. Read and follow the matching skills:

- `.agents/skills/huangshan-imu/SKILL.md` for LSM6DSL, acceleration,
  gyroscope, step counting, tilt, orientation, or motion behavior.
- `.agents/skills/huangshan-sensor-availability/SKILL.md` for generic sensor
  discovery, light/magnetometer data, capability checks, or offline fallbacks.

Skills are cumulative. A sensor App with a screen must use both its sensor skill
and `.agents/skills/huangshan-screen-ui/SKILL.md`. Never infer that a documented
sensor is physically fitted; preserve the repository's verified availability
matrix and require probe evidence before expanding it.

## Codex Pet update sources and publishing

When asked to publish or update the macOS Companion, the public `/pet/` page, or
either update feed, first read and follow
`.agents/skills/publish-vibeboard-companion-adhoc/SKILL.md`. The current release
flow is ad-hoc signed and intentionally does not require a Developer ID or Apple
notarization. Do not invent a second upload path or use a password file.

The stable public locations are:

- Public product page: `https://ldcx.tech/pet/` (an entry page, not an update feed).
- Companion update manifest: `https://ldcx.tech/static/codex-pet/companion/update-manifest.json`.
- Companion artifacts: `https://ldcx.tech/static/codex-pet/companion/`.
- Firmware update manifest: `https://ldcx.tech/static/codex-pet/firmware/releases.json`.
- Firmware artifacts: `https://ldcx.tech/static/codex-pet/firmware/`.

Local sources of truth and build inputs:

- `scripts/build_codex_pet_companion_app.command` consumes
  `VIBEBOARD_UPDATE_MANIFEST_URL`, `VIBEBOARD_RELEASE_DOWNLOAD_URL`,
  `VIBEBOARD_FIRMWARE_MANIFEST_URL`, and
  `VIBEBOARD_FIRMWARE_PUBLIC_KEY_PATH` and writes those settings into the App.
- `.local/dist/update-manifest.json` is the generated Companion release manifest;
  publish its referenced DMG and checksum before replacing the live manifest.
- `codex-pet-companion/firmware-feed/releases.json` is the repository copy of the
  firmware feed. Publish and verify a signed firmware archive before updating the
  public feed. Firmware updates require the pinned public key at
  `codex-pet-companion/keys/firmware-public.pem`; never commit or upload the private
  signing key.

Always read the live manifest before choosing a version/build, never overwrite an
existing release artifact, upload immutable artifacts first, and update the public
manifest last. After publishing, download the public manifest and artifact again,
verify the SHA-256, and confirm a built Companion's `Info.plist` contains both
manifest URLs. Do not treat the download URL embedded in the web page as the
authoritative update source.
