#include <rtthread.h>
#include "lvgl.h"
#include "gui_app_fwk.h"
#include "lv_ext_resource_manager.h"
#include "lv_ex_data.h"

#define APP_ID "codex_test"

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *tick_label;
    lv_obj_t *touch_label;
    lv_timer_t *timer;
    uint32_t tick_count;
    uint32_t touch_count;
} codex_test_t;

static codex_test_t g_codex_test;

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

static void timer_cb(lv_timer_t *timer)
{
    codex_test_t *state = (codex_test_t *)timer->user_data;

    state->tick_count++;
    if (state->tick_label)
    {
        lv_label_set_text_fmt(state->tick_label, "LVGL timer: %lu", state->tick_count);
    }
}

static void root_event_cb(lv_event_t *event)
{
    codex_test_t *state = (codex_test_t *)lv_event_get_user_data(event);

    if (LV_EVENT_CLICKED == lv_event_get_code(event))
    {
        state->touch_count++;
        if (state->touch_label)
        {
            lv_label_set_text_fmt(state->touch_label, "Touch count: %lu", state->touch_count);
        }
        rt_kprintf("[Codex_Test] touch count=%lu\n", state->touch_count);
    }
}

static void back_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED == lv_event_get_code(event))
    {
        rt_kprintf("[Codex_Test] back to Main\n");
        gui_app_run("Main");
    }
}

static lv_obj_t *create_color_block(lv_obj_t *parent, uint32_t color, lv_align_t align,
                                    lv_coord_t x_ofs, lv_coord_t y_ofs)
{
    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_set_size(block, 88, 88);
    lv_obj_align(block, align, x_ofs, y_ofs);
    set_obj_bg(block, color);
    return block;
}

static void on_start(void)
{
    rt_memset(&g_codex_test, 0, sizeof(g_codex_test));

    g_codex_test.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_codex_test.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_align(g_codex_test.root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_codex_test.root, LV_OBJ_FLAG_SCROLLABLE);
    set_obj_bg(g_codex_test.root, 0x101820);
    lv_obj_add_event_cb(g_codex_test.root, root_event_cb, LV_EVENT_CLICKED, &g_codex_test);

    lv_obj_t *title = create_label(g_codex_test.root, "Codex Test", FONT_BIGL, LV_COLOR_WHITE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *back_btn = lv_btn_create(g_codex_test.root);
    lv_obj_set_size(back_btn, 72, 36);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 12, 16);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x2b3a45), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(back_btn, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(back_btn, back_event_cb, LV_EVENT_CLICKED, RT_NULL);

    lv_obj_t *back_label = create_label(back_btn, "Back", FONT_SMALL, LV_COLOR_WHITE);
    lv_obj_center(back_label);

    lv_obj_t *subtitle = create_label(g_codex_test.root, "LCKFB SF32LB52 390x450", FONT_NORMAL,
                                      lv_color_hex(0x9bdcff));
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 62);

    create_color_block(g_codex_test.root, 0xff3030, LV_ALIGN_TOP_LEFT, 32, 112);
    create_color_block(g_codex_test.root, 0x20d060, LV_ALIGN_TOP_RIGHT, -32, 112);
    create_color_block(g_codex_test.root, 0x3078ff, LV_ALIGN_TOP_LEFT, 32, 216);
    create_color_block(g_codex_test.root, 0xffffff, LV_ALIGN_TOP_RIGHT, -32, 216);

    g_codex_test.touch_label = create_label(g_codex_test.root, "Touch count: 0", FONT_NORMAL,
                                            LV_COLOR_WHITE);
    lv_obj_align(g_codex_test.touch_label, LV_ALIGN_BOTTOM_MID, 0, -74);

    g_codex_test.tick_label = create_label(g_codex_test.root, "LVGL timer: 0", FONT_NORMAL,
                                           lv_color_hex(0xffd166));
    lv_obj_align(g_codex_test.tick_label, LV_ALIGN_BOTTOM_MID, 0, -42);

    lv_obj_t *hint = create_label(g_codex_test.root, "Tap screen. Back returns home.", FONT_SMALL,
                                  lv_color_hex(0xa8b3bd));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);

    g_codex_test.timer = lv_timer_create(timer_cb, 1000, &g_codex_test);
    rt_kprintf("[Codex_Test] start\n");
}

static void on_resume(void)
{
    rt_kprintf("[Codex_Test] resume\n");
}

static void on_pause(void)
{
    rt_kprintf("[Codex_Test] pause\n");
}

static void on_stop(void)
{
    if (g_codex_test.timer)
    {
        lv_timer_del(g_codex_test.timer);
        g_codex_test.timer = RT_NULL;
    }

    if (g_codex_test.root)
    {
        lv_obj_del(g_codex_test.root);
        g_codex_test.root = RT_NULL;
    }

    rt_kprintf("[Codex_Test] stop\n");
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
    rt_kprintf("[Codex_Test] registered\n");
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(codex_test), LV_EXT_IMG_GET(img_LiChuang), APP_ID, app_main);
