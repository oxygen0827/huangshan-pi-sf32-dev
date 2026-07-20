---
name: huangshan-screen-ui
description: Designs and reviews board-side UI for the Huangshan Pi 390x450 rounded AMOLED display. Use when creating, changing, debugging, or reviewing LVGL C UI, Runtime Lua UI, games, launchers, dashboards, manifests that generate UI, or screen screenshots in this repository.
---

# Huangshan Screen UI

## Non-negotiable target

- Design for the real CO5300 AMOLED: 390x450 px, portrait, rounded visible edges.
- Treat LVGL's rectangular canvas and the physically visible area as different.
- Keep critical content inside the correct safe area; backgrounds may be full screen.
- Read [REFERENCE.md](REFERENCE.md) before choosing dimensions or positions.

## Workflow

1. Identify the UI path: Home, built-in LVGL app, or VibeBoard Runtime app.
2. Write a vertical layout budget before code: safe-area top, header, content,
   footer/actions, gaps, safe-area bottom. The sum must fit the safe height.
3. Reuse the path's existing safe-area constants. If the file has none, define
   named local constants from the reference values; do not scatter edge numbers.
4. Use flex/grid or parent-relative alignment where content is dynamic. Absolute
   positioning is acceptable only when all dimensions are centralized.
5. Keep critical text, status, navigation, and touch targets within the safe area.
   Permit only non-interactive decoration to enter rounded corners.
6. Plan for the longest realistic English/Chinese value, empty/loading/error
   states, maximum item count, and dynamic numeric values before implementation.
7. Make lists/grids scroll. Keep touch targets at least 44x44 px with separation.
8. Run the audit before building:
   `sh .agents/skills/huangshan-screen-ui/scripts/audit-ui.sh <changed-ui-files>`
9. Build and test in proportion to the change. For final UI acceptance, flash the
   board and inspect a straight-on photo/video; a rectangular screenshot is not
   sufficient evidence for rounded-edge visibility.

## Required review

- No clipped, overlapping, off-screen, or corner-obscured critical content.
- Header, bottom status/actions, and right-side controls are fully visible.
- Dynamic labels have bounded width, wrapping/ellipsis, or stable containers.
- Primary navigation does not depend on a small top-corner Home button; retain K1
  and use the established left-edge swipe when appropriate.
- Object count, image memory, animation, and redraw frequency suit an MCU target.
- Runtime apps stay within documented Lua handle/component limits.
- Any audit warning is either fixed or explicitly justified in the handoff.

## Handoff

Report the safe-area budget used, audit/build results, and whether real-board
visual verification was completed. If it was not, state that residual risk.
