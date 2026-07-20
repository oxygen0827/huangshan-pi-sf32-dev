# Huangshan Pi Screen Reference

## Hardware and coordinate model

| Property | Value |
| --- | --- |
| Panel | CO5300 AMOLED |
| Resolution | 390x450 px |
| Orientation | Portrait |
| LVGL canvas | Full rectangle: x=0..389, y=0..449 |
| Visible shape | Rounded portrait rectangle; corners are partially obscured |
| Touch | FT6146 |
| GUI stack | RT-Thread, SiFli SDK, LVGL 8 |
| Color target | RGB565 |

## Verified safe areas

| UI path | Insets L/R/T/B | Safe rectangle |
| --- | --- | --- |
| Home launcher | 28 / 28 / 34 / 34 px | x=28..361, y=34..415; 334x382 px |
| VibeBoard Runtime | 30 / 30 / 36 / 36 px | x=30..359, y=36..413; 330x378 px |
| New/unknown board UI | Start with 30 / 30 / 36 / 36 px | 330x378 px until verified on hardware |

Existing constants:

- `HUANGSHAN_HOME_SAFE_LEFT/RIGHT/TOP/BOTTOM` in
  `src/gui_apps/main/app_mainmenu.c`.
- `VB_SCREEN_SAFE_LEFT/RIGHT/TOP/BOTTOM/WIDTH/HEIGHT` in
  `src/gui_apps/VibeBoard_Runtime/main.c`.

These are content-safe insets, not a clipping mask. Full-screen backgrounds,
gameplay, and edge gestures may use the full canvas. Important readable or
interactive content must remain inside the safe rectangle.

## Layout budgeting

Budget from 378 px of Runtime safe height, for example:

```text
header          48
gap              8
content        250
gap              8
actions/status  52
gaps/slack       12
total          378
```

Do not treat this example as a template. Derive a budget for the actual page.
If the content cannot fit, scroll the content region instead of shrinking touch
targets or moving controls into the rounded corners.

## Component constraints

- Touch target: minimum 44x44 px.
- Spacing: prefer a consistent 4/8 px rhythm.
- Text: bound label width; test long English, Chinese, numbers, empty and error.
- Dynamic rows: keep value columns stable so updates do not reflow the page.
- Navigation: K1 is the reliable return-home control. Left-edge swipe may be a
  secondary path. Do not rely on a tiny top-right Home button.
- Lists and app grids: scroll when content exceeds the safe content region.
- AMOLED: prefer dark backgrounds and high contrast; avoid broad transparency,
  blur, heavy shadow, and large unbudgeted bitmaps.
- Runtime Lua: at most 96 tracked LVGL handles and 8 high-level `vibe_ui_*`
  components per app.

## Evidence hierarchy

1. Straight-on real-board photo/video after flashing.
2. Runtime touch and interaction test on the board.
3. Simulator/screenshot for rectangular layout and overlap checks.
4. Static coordinate review.

Only the first level validates whether the bezel and rounded visible edges hide
content. When hardware is unavailable, report this as unverified rather than
claiming the layout is final.

## Project references

- `docs/ai-ui/lvgl-system-contract.md`
- `docs/ai-ui/visual-review-checklist.md`
- `docs/runtime-app-development-notes.md`
- `README.md`
