# LSM6DSL App Contract

## Verified hardware

| Item | Contract |
| --- | --- |
| Device | LSM6DSL |
| Bus | I2C3 |
| Verified address | `0x6a` |
| Runtime channels | accelerometer, gyroscope, step counter |
| Acceleration unit | mg |
| Gyroscope unit | mdps |
| Step unit | count |

Runtime probes before initialization and owns the RT-Device handles. Apps must
not repeat bus or sensor setup. The driver also checks alternate address `0x6b`,
but the current physical board responds at `0x6a`.

## Runtime JSON

Read with `vb_runtime_sensors` or the serial/BLE Runtime transport:

```json
{
  "api": "vibeboard-huangshan-sensors/v1",
  "available": 1,
  "ready": 1,
  "count": 3,
  "acce": {"ok": 1, "x": -9, "y": 48, "z": -986},
  "gyro": {"ok": 1, "x": -70, "y": -18760, "z": -13720},
  "step": {"ok": 1, "count": 0}
}
```

Check each channel's `ok`; do not infer availability from a zero value. The
top-level `ready` means at least one built-in sensor channel returned data.

## Runtime App APIs

Manifest capabilities:

- `sensor.acce`
- `sensor.gyro`
- `sensor.step`

Lua helpers:

- `vibe_sensor_label(label, "acce")`: one accelerometer snapshot.
- `vibe_sensor_label(label, "gyro")`: one gyroscope snapshot.
- `vibe_sensor_label(label, "step")`: one step-count snapshot.
- `vibe_imu_lab("Title")`: native continuously sampled attitude UI.

The high-level IMU implementation lives in
`src/gui_apps/VibeBoard_Runtime/main.c`; its example package is
`scripts/runtime_apps/imu_lab/`. Do not copy its driver initialization into Lua.

## Portrait mapping and calibration

The LSM6DSL axes are rotated 90 degrees relative to the portrait display. The
verified attitude dial maps them as:

```text
screen_x = -acce.y
screen_y =  acce.x
```

Use a stationary calibration window for accelerometer X/Y zero and gyroscope
X/Y/Z bias. Apply filtering before moving UI objects; the existing dial uses a
3:1 previous/new integer low-pass filter. Revalidate signs for any different
physical mounting or screen orientation.

## Verification

- `./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --sensors-only`
- `vb_runtime_sensor_probe` confirms physical I2C response.
- `vb_runtime_sensors` confirms `acce.ok`, `gyro.ok`, and expected units.
- Test stationary noise, positive/negative X/Y tilt, rotation, calibration, and
  the offline/read-error UI before considering an IMU App complete.
