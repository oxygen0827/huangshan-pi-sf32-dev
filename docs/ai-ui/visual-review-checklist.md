# Huangshan Pi Visual Review Checklist

Use this checklist with a real board photo, short video, or simulator screenshot.
Ask for specific fixes rather than a general opinion.

## Blocking Usability

- Text is clipped, overlapped, or unreadable.
- Buttons, cards, charts, or badges overlap each other.
- Critical content is hidden near the rounded display edge or bezel.
- The primary action or current status is unclear.
- Back/navigation controls are missing or too small.

## Embedded Touch

- Main touch targets are at least 44x44 px.
- Adjacent controls have enough spacing to avoid accidental taps.
- Tap feedback is visible enough on AMOLED.
- Dynamic labels do not jump or resize the whole layout.

## Visual Consistency

- Spacing follows a simple 4/8 px rhythm.
- Similar cards, buttons, and status labels share styling.
- Font hierarchy is limited and readable.
- Status colors match device semantics: ready, warning, danger, disabled.
- The page does not look like a web dashboard shrunk onto a watch-sized screen.

## Performance And Memory

- No large unbudgeted image assets.
- No blur, glassmorphism, or broad transparent overlays.
- No full-screen high-frequency redraw without a reason.
- Charts, lists, and animations have bounded object counts.
- Timers are stopped when the app exits.

## Suggested AI Review Prompt

```text
Review this Huangshan Pi 390x450 LVGL screen.
Return:
1. Blocking issues that can cause misread or mistap.
2. Visual consistency issues.
3. MCU performance or memory risks.
4. Concrete code changes, naming the component/style/page area to edit.
```
