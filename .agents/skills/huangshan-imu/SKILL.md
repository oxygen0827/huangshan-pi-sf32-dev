---
name: huangshan-imu
description: Builds and reviews Huangshan Pi apps that use the verified onboard LSM6DSL accelerometer, gyroscope, step counter, tilt, orientation, or motion data. Use when an app, manifest, Runtime Lua/C code, test, or UI mentions IMU, LSM6DSL, accelerometer, acceleration, gyro, gyroscope, tilt, roll, pitch, motion, shake, gesture, step counting, sensor.acce, sensor.gyro, or sensor.step.
---

# Huangshan Pi IMU

## Required context

Read [references/contract.md](references/contract.md) before choosing an API,
units, axis mapping, calibration, or update strategy.

## Workflow

1. Classify the app's data need:
   - Use `vibe_imu_lab("Title")` for the existing live attitude dial, motion
     state, filtering, calibration, and periodic sampling.
   - Use manifest components and `vibe_sensor_label(label, "acce|gyro|step")`
     only for startup snapshot displays.
   - Treat custom continuous raw IMU sampling from Runtime Lua as a firmware
     capability change; the current Lua host has no raw sensor polling API.
2. Declare only the capabilities used: `sensor.acce`, `sensor.gyro`, and/or
   `sensor.step`. Include `display` and `touch` only when the app uses them.
3. Reuse Runtime sensor initialization. Do not initialize I2C3 or LSM6DSL from
   an App, change its ODR, or bypass the shared RT-Device sensor handles.
4. Check per-channel availability before using data. Handle `IMU OFFLINE`, read
   failure, and partial accelerometer/gyro availability without crashing.
5. Preserve native units until a named conversion boundary: acceleration is mg,
   angular rate is mdps, and step is a count.
6. Calibrate bias for orientation or motion thresholds. Do not hard-code a
   stationary zero from one board sample.
7. For screen UI, also apply `$huangshan-screen-ui`; hardware and layout skills
   are cumulative.
8. Validate the package, run the sensor check, build when firmware changed, and
   inspect live board behavior across still, tilt, rotation, and read failure.

## Review gate

- Manifest capabilities match actual sensor use.
- No duplicate driver/I2C initialization exists in App code.
- Snapshot and continuous-update behavior are not confused.
- Axis mapping and units are explicit at every derived calculation.
- Filtering, calibration, and thresholds use bounded integer arithmetic.
- Missing or failed readings produce a stable offline state.
- Handoff states which sensor paths were verified on the physical board.
