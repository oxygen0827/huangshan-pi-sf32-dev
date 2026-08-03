# Huangshan Pi Screen Reference

## Canonical screen profile

| Property | Value |
| --- | --- |
| Panel | CO5300 AMOLED |
| Logical resolution | 390x450 px |
| Pixel aspect | 1:1 square pixels |
| Canvas aspect | 13:15 portrait (0.8667 width/height) |
| Orientation | Portrait |
| LVGL canvas | Full rectangle: x=0..389, y=0..449 |
| Physical visible shape | Strongly rounded portrait rectangle; corner pixels fall outside the visible panel stack outline |
| Review-only contour | 390x450 rounded rectangle, 72 px corner radius |
| Touch | FT6146 |
| GUI stack | RT-Thread, SiFli SDK, LVGL 8 |
| Color target | RGB565 |

The 72 px radius is a conservative visual-review approximation calibrated from
a real powered-board photo on 2026-07-24. The photo was oblique and includes
cover-glass perspective, so this is not a measured panel mask or a firmware
clipping constant. Use it to expose risky corner placement during design. The
rectangular 390x450 framebuffer remains the implementation source of truth.

Use [the canonical guide overlay](assets/huangshan-screen-guide.svg) when
authoring or reviewing a screen image. It has an exact 390x450 view box and
contains the approximate visible contour, Runtime safe rectangle, recommended
focal zone, and center lines.

## Image and mockup contract

| Artifact | Required working size | Export rule |
| --- | --- | --- |
| Full-screen background/device bitmap | 390x450 px | Export the full rectangular canvas; do not bake in bezel or corner transparency |
| Screen mockup/review frame | 390x450 px | Apply the 72 px rounded contour only as a preview mask |
| 2x generation canvas | 780x900 px | Downsample to 390x450 before integration |
| 3x generation canvas | 1170x1350 px | Downsample to 390x450 before integration |
| Pixel-art sprite | Native pixel dimensions | Scale with nearest-neighbor when crisp pixels are intentional |
| Painted/photo-style art | Native or integer-multiple canvas | Downsample with a high-quality continuous-tone filter |

Never request or crop a full-screen image as 390x390, 450x450, or a generic
square smartwatch face. Do not measure the blue screen or UI positions from the
outer black case in a camera photo. Perspective changes apparent widths and
heights; known logical coordinates remain authoritative.

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

## Photo-calibrated visual zones

| Zone | Logical bounds | Use |
| --- | --- | --- |
| Full-bleed canvas | x=0..389, y=0..449; 390x450 px | Background color/art, particles, and non-critical decoration |
| Runtime critical-safe rectangle | x=30..359, y=36..413; 330x378 px | All readable text, status, navigation, and touch targets |
| Recommended focal zone | x=48..341, y=64..385; 294x322 px | Main character, object, or other dominant visual subject |
| Top corner band | y=0..35 | No critical content; background/decoration only |
| Bottom corner band | y=414..449 | No critical content; background/decoration only |

The focal zone is a composition recommendation, not a clip. A large subject may
extend beyond it when its face, silhouette, and meaningful details remain inside
the Runtime critical-safe rectangle. This distinction keeps characters visually
large without putting eyes, labels, or controls into the glass curvature.

At the top and bottom, usable width narrows rapidly near the corners. Keep a
full-screen background behind the entire framebuffer, but treat the corner bands
as expendable. In particular, do not place a title, ID, battery state, or Home
control against x=0 or x=389 just because it is visible in a rectangular capture.

The 2026-07-24 Codex Companion photo confirms that the current Runtime insets
keep the left title and right status visible, while the outer blue background is
partially lost at the rounded visible boundary. It also shows why the casing must
not be used as the canvas boundary and why a normal screenshot cannot prove
corner visibility.

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

## Board font pipeline and legibility

The 390x450 firmware path enables the SiFli resource manager and FreeType font
manager. Built-in and VibeBoard Runtime C screens should normally include
`lv_ext_resource_manager.h` and set type through:

```c
lv_ext_set_local_font(label, FONT_NORMAL, lv_color_hex(0xf8fafc));
```

This is not interchangeable with directly assigning an LVGL bitmap font:

```c
/* Avoid by default on this target. */
lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
```

The direct bitmap call bypasses the established managed-font path. During the
2026-08-03 Codex Pet usage-screen review, direct Montserrat 12/16/20/28 px fonts
produced dotted, broken-looking strokes on the real AMOLED and pulled about 70
KiB of additional font data into the firmware. Static coordinate checks and a
successful build did not reveal the legibility failure. Returning to
`lv_ext_set_local_font` restored clear text on target.

For this resolution, the established managed scale is:

| Role | Constant | Nominal size |
| --- | --- | --- |
| Supporting label / status | `FONT_SMALL` | 16 px |
| Body / secondary value | `FONT_NORMAL` | 20 px |
| Section title / metric value | `FONT_SUBTITLE` | 24 px |
| Page title / primary value | `FONT_TITLE` | 28 px |
| Exceptional display value | `FONT_BIGL` | 36 px |

Use 16 px as the default readability floor for information. Do not solve an
overfull page by dropping important text to 12 px. Shorten copy, remove repeated
headings, split dynamic metrics into stable columns, or scroll the content.

Label boxes must be sized for the managed font's actual line height, not merely
the nominal size or a previous bitmap font. Allow roughly 4 px or more of
vertical slack where fixed positioning is necessary. A target photo must confirm
that descenders, curved strokes, and small numerals remain continuous and filled.

Direct bitmap fonts remain available for an intentional pixel-art look or a
proven memory/performance need. Such an exception requires a before/after board
photo, longest-string check, firmware-size comparison, and an explicit handoff
note. A desktop preview alone is not evidence.

## Evidence hierarchy

1. Straight-on real-board photo/video after flashing, with the active display in
   focus and the complete glass outline visible. Review both safe-area placement
   and glyph quality at normal viewing scale, not only a zoomed crop.
2. Runtime touch and interaction test on the board.
3. Simulator/screenshot for rectangular layout and overlap checks.
4. Static coordinate review.

Only the first level validates whether the bezel and rounded visible edges hide
content. When hardware is unavailable, report this as unverified rather than
claiming the layout is final.

An oblique photo is still useful for confirming gross corner risk and real-world
legibility, but it must not be used to derive exact pixel distances, font sizes,
or corner radii. Capture a straight-on follow-up image for final dimensional
calibration.

## Project references

- `docs/ai-ui/lvgl-system-contract.md`
- `docs/ai-ui/visual-review-checklist.md`
- `docs/runtime-app-development-notes.md`
- `README.md`
