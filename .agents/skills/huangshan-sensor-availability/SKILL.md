---
name: huangshan-sensor-availability
description: Designs and reviews Huangshan Pi sensor discovery, capability checks, missing-hardware fallbacks, and generic sensor dashboards. Use when an app, manifest, Runtime Lua/C code, test, or UI mentions built-in sensors, sensor probing, sensor availability, LTR303, ambient light, lux, MMC56X3, magnetometer, compass, sensor.light, sensor.mag, missing sensor data, offline sensor states, or vb_runtime_sensors.
---

# Huangshan Sensor Availability

## Required context

Read [references/contract.md](references/contract.md) before declaring a sensor
requirement or interpreting Runtime sensor JSON.

## Workflow

1. Start from the verified hardware matrix, then confirm current runtime state
   with `vb_runtime_sensors` when a board is available.
2. Interpret availability per channel using `<sensor>.ok`. Do not treat zero as
   absence, and do not treat top-level `ready` as proof that every sensor works.
3. Use `vb_runtime_sensor_probe` when behavior conflicts with the known matrix.
   Distinguish a physically absent device from an uncompiled driver or read error.
4. Declare `sensor.light` or `sensor.mag` only when the app has an explicit
   unavailable state or the target hardware was separately verified to contain
   that device. Do not make either capability mandatory for this board revision.
5. Render absent data as `Unavailable`/`Not fitted`/`--`, never as a believable
   `0 lux` or `0,0,0` measurement.
6. Prefer the LSM6DSL channels for a useful sensor App on the current board. Also
   apply `$huangshan-imu` for motion behavior and `$huangshan-screen-ui` for UI.
7. Validate both online and missing-sensor paths before installation and report
   the physical board evidence in the handoff.

## Review gate

- Requirements do not promise LTR303 or MMC56X3 on the current board.
- Every sensor value is guarded by its own `ok` field.
- Missing, read-error, loading, and valid-zero states are distinguishable.
- Probe/init code never asserts merely because optional hardware is absent.
- Generic dashboards remain useful when only LSM6DSL channels are online.
- Handoff lists detected devices, unavailable devices, and test transport.
