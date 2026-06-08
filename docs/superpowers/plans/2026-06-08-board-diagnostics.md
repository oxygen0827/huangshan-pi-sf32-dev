# Board Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a built-in `Board Diagnostics` LVGL app for Huangshan Pi board bring-up.

**Architecture:** Create a new self-contained app under `src/gui_apps/Board_Diagnostics/`, following the existing `Codex_Test` and `LC_Hello_World` built-in app pattern. The app owns its LVGL objects and timer, registers through `BUILTIN_APP_EXPORT`, and returns to `Main` without changing the existing launcher.

**Tech Stack:** C, RT-Thread, LVGL, SiFli GUI app framework, SCons.

---

## File Map

- Create `src/gui_apps/Board_Diagnostics/SConscript`: compiles the new app when LVGL support is enabled.
- Create `src/gui_apps/Board_Diagnostics/main.c`: app lifecycle, UI layout, timer, touch handler, and back navigation.
- Modify `src/resource/strings/en_us.json`: add `board_diagnostics`.
- Modify `src/resource/strings/zh_cn.json`: add `board_diagnostics`.
- Use existing `src/resource/images/common/ezip/img_LiChuang.png` resource through `LV_IMG_DECLARE(img_LiChuang)`.

## Task 1: Add Board Diagnostics App

**Files:**
- Create: `src/gui_apps/Board_Diagnostics/SConscript`
- Create: `src/gui_apps/Board_Diagnostics/main.c`
- Modify: `src/resource/strings/en_us.json`
- Modify: `src/resource/strings/zh_cn.json`

- [ ] **Step 1: Create the SCons module**

Create `src/gui_apps/Board_Diagnostics/SConscript`:

```python
from building import *

cwd = GetCurrentDir()
objs = []

if GetDepend('PKG_USING_LITTLEVGL2RTT'):
    objs = Glob('*.c')
    group = DefineGroup('App_Board_Diagnostics', objs, depend=['PKG_USING_LITTLEVGL2RTT'], CPPPATH=[''], LOCAL_CCFLAGS='')

Return('objs')
```

- [ ] **Step 2: Create the app implementation**

Create `src/gui_apps/Board_Diagnostics/main.c` with:

```c
#include <rtthread.h>
#include "lvgl.h"
#include "gui_app_fwk.h"
#include "lv_ext_resource_manager.h"
#include "lv_ex_data.h"

#define APP_ID "board_diagnostics"

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *uptime_label;
    lv_obj_t *tick_label;
    lv_obj_t *touch_label;
    lv_obj_t *coord_label;
    lv_timer_t *timer;
    uint32_t seconds;
    uint32_t tick_count;
    uint32_t touch_count;
} board_diag_t;

static board_diag_t g_board_diag;

static void set_obj_bg(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, uint16_t font_size,
                              lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_ext_set_local_font(label, font_size, color);
    return label;
}

static lv_obj_t *create_metric_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                    const char *title, const char *value,
                                    lv_obj_t **value_label)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 170, 76);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    set_obj_bg(card, 0x172033);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2d3b52), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title_label = create_label(card, title, FONT_SMALL, lv_color_hex(0x9fb2c8));
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_t *value_obj = create_label(card, value, FONT_NORMAL, LV_COLOR_WHITE);
    lv_obj_align(value_obj, LV_ALIGN_BOTTOM_LEFT, 8, -8);

    if (value_label)
    {
        *value_label = value_obj;
    }

    return card;
}

static void create_color_block(lv_obj_t *parent, lv_coord_t x, uint32_t color)
{
    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_set_size(block, 78, 42);
    lv_obj_align(block, LV_ALIGN_TOP_LEFT, x, 282);
    set_obj_bg(block, color);
}

static void timer_cb(lv_timer_t *timer)
{
    board_diag_t *state = (board_diag_t *)timer->user_data;

    state->seconds++;
    state->tick_count++;

    if (state->uptime_label)
    {
        lv_label_set_text_fmt(state->uptime_label, "Uptime %lu s", state->seconds);
    }

    if (state->tick_label)
    {
        lv_label_set_text_fmt(state->tick_label, "%lu", state->tick_count);
    }
}

static void root_event_cb(lv_event_t *event)
{
    board_diag_t *state = (board_diag_t *)lv_event_get_user_data(event);

    if (LV_EVENT_CLICKED == lv_event_get_code(event))
    {
        lv_point_t point;

        state->touch_count++;
        lv_indev_get_point(lv_indev_get_act(), &point);

        if (state->touch_label)
        {
            lv_label_set_text_fmt(state->touch_label, "%lu", state->touch_count);
        }

        if (state->coord_label)
        {
            lv_label_set_text_fmt(state->coord_label, "%d,%d", point.x, point.y);
        }

        rt_kprintf("[Board_Diagnostics] touch count=%lu x=%d y=%d\n",
                   state->touch_count, point.x, point.y);
    }
}

static void back_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED == lv_event_get_code(event))
    {
        rt_kprintf("[Board_Diagnostics] back to Main\n");
        gui_app_run("Main");
    }
}

static void on_start(void)
{
    rt_memset(&g_board_diag, 0, sizeof(g_board_diag));

    g_board_diag.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_board_diag.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_align(g_board_diag.root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_board_diag.root, LV_OBJ_FLAG_SCROLLABLE);
    set_obj_bg(g_board_diag.root, 0x070b12);
    lv_obj_add_event_cb(g_board_diag.root, root_event_cb, LV_EVENT_CLICKED, &g_board_diag);

    lv_obj_t *back_btn = lv_btn_create(g_board_diag.root);
    lv_obj_set_size(back_btn, 72, 36);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 12, 14);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x23324a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(back_btn, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(back_btn, back_event_cb, LV_EVENT_CLICKED, RT_NULL);

    lv_obj_t *back_label = create_label(back_btn, "Back", FONT_SMALL, LV_COLOR_WHITE);
    lv_obj_center(back_label);

    lv_obj_t *title = create_label(g_board_diag.root, "Board Diagnostics", FONT_BIGL, LV_COLOR_WHITE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 22, 18);

    lv_obj_t *subtitle = create_label(g_board_diag.root, "SF32LB52 / CO5300 / FT6146",
                                      FONT_SMALL, lv_color_hex(0x93c5fd));
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 20, 58);

    g_board_diag.uptime_label = create_label(g_board_diag.root, "Uptime 0 s", FONT_SMALL,
                                             lv_color_hex(0xfacc15));
    lv_obj_align(g_board_diag.uptime_label, LV_ALIGN_TOP_MID, 0, 84);

    create_metric_card(g_board_diag.root, 22, 118, "Touch Count", "0", &g_board_diag.touch_label);
    create_metric_card(g_board_diag.root, 198, 118, "Last XY", "--,--", &g_board_diag.coord_label);
    create_metric_card(g_board_diag.root, 22, 202, "LVGL Tick", "0", &g_board_diag.tick_label);
    create_metric_card(g_board_diag.root, 198, 202, "Panel", "390x450", RT_NULL);

    create_color_block(g_board_diag.root, 22, 0xff3030);
    create_color_block(g_board_diag.root, 112, 0x20d060);
    create_color_block(g_board_diag.root, 202, 0x3078ff);
    create_color_block(g_board_diag.root, 292, 0xffffff);

    lv_obj_t *hint = create_label(g_board_diag.root, "Tap screen to test touch. Back returns home.",
                                  FONT_SMALL, lv_color_hex(0xa8b3bd));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -34);

    g_board_diag.timer = lv_timer_create(timer_cb, 1000, &g_board_diag);
    rt_kprintf("[Board_Diagnostics] start\n");
}

static void on_resume(void)
{
    rt_kprintf("[Board_Diagnostics] resume\n");
}

static void on_pause(void)
{
    rt_kprintf("[Board_Diagnostics] pause\n");
}

static void on_stop(void)
{
    if (g_board_diag.timer)
    {
        lv_timer_del(g_board_diag.timer);
        g_board_diag.timer = RT_NULL;
    }

    if (g_board_diag.root)
    {
        lv_obj_del(g_board_diag.root);
        g_board_diag.root = RT_NULL;
    }

    rt_kprintf("[Board_Diagnostics] stop\n");
}

static void msg_handler(gui_app_msg_type_t msg, void *param)
{
    switch (msg)
    {
    case GUI_APP_MSG_ONSTART:
        on_start();
        break;
    case GUI_APP_MSG_ONRESUME:
        on_resume();
        break;
    case GUI_APP_MSG_ONPAUSE:
        on_pause();
        break;
    case GUI_APP_MSG_ONSTOP:
        on_stop();
        break;
    default:
        break;
    }
}

LV_IMG_DECLARE(img_LiChuang);

static int app_main(intent_t i)
{
    gui_app_regist_msg_handler(APP_ID, msg_handler);
    rt_kprintf("[Board_Diagnostics] registered\n");
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(board_diagnostics), LV_EXT_IMG_GET(img_LiChuang), APP_ID, app_main);
```

- [ ] **Step 3: Add English string**

Modify `src/resource/strings/en_us.json` so the last entries include:

```json
    "lckfb": "LCKFB",
    "codex_test": "Codex Test",
    "board_diagnostics": "Board Diagnostics"
```

- [ ] **Step 4: Add Chinese string**

Modify `src/resource/strings/zh_cn.json` so the last entries include:

```json
    "lckfb": "立创开发板",
    "codex_test": "Codex测试",
    "board_diagnostics": "板级诊断"
```

- [ ] **Step 5: Build**

Run:

```bash
./scripts/build.sh
```

Expected: SCons completes successfully and includes `Board_Diagnostics/main.c`
in the build output.

- [ ] **Step 6: Commit**

Run:

```bash
git add src/gui_apps/Board_Diagnostics src/resource/strings/en_us.json src/resource/strings/zh_cn.json
git commit -m "feat: add board diagnostics app"
```

## Task 2: Hardware Verification

**Files:**
- Modify only if verification exposes a build or app lifecycle issue.

- [ ] **Step 1: Flash**

Run:

```bash
./scripts/flash.sh /dev/cu.usbserial-110
```

Expected: download script completes without UART errors.

- [ ] **Step 2: Monitor boot and app lifecycle**

Run:

```bash
SECONDS_TO_CAPTURE=20 ./scripts/monitor.sh /dev/cu.usbserial-110
```

Expected after opening the app from the launcher:

```text
[Board_Diagnostics] registered
[Board_Diagnostics] start
[Board_Diagnostics] resume
```

- [ ] **Step 3: Verify touch**

Tap the screen.

Expected serial log:

```text
[Board_Diagnostics] touch count=1 x=<x> y=<y>
```

Expected display behavior: `Touch Count` increments and `Last XY` changes.

- [ ] **Step 4: Fix only observed issues**

If build or hardware verification fails, change only the smallest app-local code
needed to restore the planned behavior, then rerun the failed command.

- [ ] **Step 5: Commit verification fix if needed**

If code changed during verification:

```bash
git add src/gui_apps/Board_Diagnostics src/resource/strings/en_us.json src/resource/strings/zh_cn.json
git commit -m "fix: verify board diagnostics on hardware"
```
