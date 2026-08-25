---
name: huangshan-screen-ui
description: Designs, generates, and reviews board-side UI and screen-sized image assets for the Huangshan Pi 390x450 rounded AMOLED display. Use when creating, changing, debugging, or reviewing LVGL C UI, Runtime Lua UI, games, launchers, dashboards, manifests that generate UI, backgrounds, sprites, screen mockups, preview images, or display screenshots in this repository.
---

# Huangshan Screen UI

## Non-negotiable target

- Design for the real CO5300 AMOLED: exactly 390x450 logical px, 13:15
  portrait aspect ratio, with strongly rounded visible edges.
- Treat LVGL's rectangular canvas and the physically visible area as different.
- Author full-screen images at 390x450 or an exact integer multiple. Do not use
  a square smartwatch canvas or infer dimensions from the outer case/bezel.
- Keep critical content inside the correct safe area; backgrounds may be full screen.
- Read [REFERENCE.md](REFERENCE.md) before choosing dimensions or positions.
- For image generation or visual review, overlay
  [the screen guide](assets/huangshan-screen-guide.svg). Its 72 px corner radius
  is a conservative photo-calibrated review mask, not a panel-controller value.

## Workflow

1. Identify the artifact and UI path: device image asset, visual mockup, Home,
   built-in LVGL app, or VibeBoard Runtime app.
2. For a bitmap or mockup, set the document to 390x450 px or an exact 2x/3x
   multiple before composing. Keep critical content inside the path's safe area
   and the main subject inside the recommended focal zone from the reference.
   Review through the screen guide, then remove the guide and preview mask from
   the exported device asset so the background still fills the rectangular canvas.
3. Write a vertical layout budget before code: safe-area top, header, content,
   footer/actions, gaps, safe-area bottom. The sum must fit the safe height.
4. Reuse the path's existing safe-area constants. If the file has none, define
   named local constants from the reference values; do not scatter edge numbers.
5. Use flex/grid or parent-relative alignment where content is dynamic. Absolute
   positioning is acceptable only when all dimensions are centralized.
6. Keep critical text, status, navigation, and touch targets within the safe area.
   Permit only non-interactive decoration to enter rounded corners.
7. Plan for the longest realistic English/Chinese value, empty/loading/error
   states, maximum item count, and dynamic numeric values before implementation.
8. Make lists/grids scroll. Keep touch targets at least 44x44 px with separation.
9. Run the audit before building:
   `sh .agents/skills/huangshan-screen-ui/scripts/audit-ui.sh <changed-ui-files>`
10. Build and test in proportion to the change. For final UI acceptance, flash the
   board and inspect a straight-on photo/video; a rectangular screenshot is not
   sufficient evidence for rounded-edge visibility.

## Required review

- No clipped, overlapping, off-screen, or corner-obscured critical content.
- Screen assets and mockups preserve the 390x450 (13:15) canvas; the device
  export does not accidentally bake in a bezel, guide, or rounded alpha mask.
- Header, bottom status/actions, and right-side controls are fully visible.
- Dynamic labels have bounded width, wrapping/ellipsis, or stable containers.
- Primary navigation does not depend on a small top-corner Home button; retain K1
  and use the established left-edge swipe when appropriate.
- Object count, image memory, animation, and redraw frequency suit an MCU target.
- Runtime apps stay within documented Lua handle/component limits.
- Any audit warning is either fixed or explicitly justified in the handoff.

## Handoff

Report the canvas/export dimensions, safe-area budget used, audit/build results,
and whether real-board visual verification was completed. If it was not, state
that residual risk.
