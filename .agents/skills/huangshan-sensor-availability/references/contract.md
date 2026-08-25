# Built-in Sensor Availability Contract

## Current physical board

Verified on `sf32lb52-lchspi-ulp` with SiFli SDK 2.5.0:

| Sensor | Bus/address | Current result | App policy |
| --- | --- | --- | --- |
| LSM6DSL IMU | I2C3 `0x6a` | Present and readable | Supported |
| LTR303 ambient light | I2C3 `0x29` | No response | Optional, show unavailable |
| MMC56X3 magnetometer | I2C3 `0x30` | No response | Optional, show unavailable |

Do not copy the official sensor example's unconditional LTR303 initialization;
it asserts when the device is absent. Runtime probes first and initializes only
responding devices.

## JSON semantics

API: `vibeboard-huangshan-sensors/v1`

```json
{
  "available": 1,
  "ready": 1,
  "count": 3,
  "light": {"ok": 0, "lux": 0},
  "mag": {"ok": 0, "x": 0, "y": 0, "z": 0},
  "acce": {"ok": 1, "x": -9, "y": 48, "z": -986},
  "gyro": {"ok": 1, "x": -70, "y": -18760, "z": -13720},
  "step": {"ok": 1, "count": 0}
}
```

- `available`: the Runtime sensor API is compiled.
- `ready`: at least one channel returned a reading.
- `count`: number of channels whose `ok` is true, not physical chip count.
- Child `ok`: authoritative availability for that measurement.
- Numeric fields remain present when `ok=0`; ignore their values.

## App-facing names

| Measurement | Manifest capability | Lua snapshot selector |
| --- | --- | --- |
| Light | `sensor.light` | `light` |
| Magnetometer | `sensor.mag` | `mag` |
| Accelerometer | `sensor.acce` | `acce` or `accel` |
| Gyroscope | `sensor.gyro` | `gyro` |
| Step count | `sensor.step` | `step` |

`vibe_sensor_label` reads one snapshot when the script executes. The generic
example is `scripts/runtime_apps/sensor_dash/`; it must tolerate absent light and
magnetometer channels. For continuous custom sampling, extend the Runtime host
API or reuse an existing native high-level feature rather than inventing a Lua
binding.

## Diagnostics

- `vb_runtime_sensor_probe`: logs I2C3 probe results for known addresses.
- `vb_runtime_sensors`: returns the complete JSON snapshot.
- `./scripts/runtime_install_serial.sh /dev/cu.usbserial-13220 --sensors-only`:
  reads and validates the current hardware combination.
- Reliability checks require at least one online channel and consistency between
  `count`, `ready`, and the child `ok` fields.
