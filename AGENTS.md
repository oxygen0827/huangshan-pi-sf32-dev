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
