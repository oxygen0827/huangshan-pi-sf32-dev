#include <rtthread.h>
#include <rtdevice.h>
#include "lvgl.h"
#include "gui_app_fwk.h"
#include "lv_ext_resource_manager.h"
#include "lv_ex_data.h"
#include "hs_ui_components.h"
#include "hs_ui_theme.h"

#define APP_ID "huangshan_ui_lab"
#define HS_UI_TREND_POINTS 18

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *tick_label;
    lv_obj_t *touch_label;
    lv_obj_t *status_badge;
    lv_obj_t *chart;
    lv_chart_series_t *series;
    lv_timer_t *timer;
    uint32_t ticks;
    uint32_t touches;
} huangshan_ui_lab_t;

static huangshan_ui_lab_t g_ui_lab;

static void back_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED == lv_event_get_code(event))
    {
        rt_kprintf("[Huangshan_UI_Lab] back to Main\n");
        gui_app_run("Main");
    }
}

static void root_event_cb(lv_event_t *event)
{
    huangshan_ui_lab_t *state = (huangshan_ui_lab_t *)lv_event_get_user_data(event);

    if (LV_EVENT_CLICKED == lv_event_get_code(event))
    {
        state->touches++;
        if (state->touch_label)
        {
            lv_label_set_text_fmt(state->touch_label, "Touch %lu", state->touches);
        }
        rt_kprintf("[Huangshan_UI_Lab] touch count=%lu\n", state->touches);
    }
}

static void timer_cb(lv_timer_t *timer)
{
    huangshan_ui_lab_t *state = (huangshan_ui_lab_t *)timer->user_data;

    state->ticks++;
    if (state->tick_label)
    {
        lv_label_set_text_fmt(state->tick_label, "Tick %lu", state->ticks);
    }

    if (state->chart && state->series)
    {
        int32_t value = 36 + (int32_t)((state->ticks * 7U) % 34U);
        lv_chart_set_next_value(state->chart, state->series, value);
    }
}

static void create_header(huangshan_ui_lab_t *state)
{
    lv_obj_t *back_btn = hs_ui_action_button_create(state->root, "Back", false);
    lv_obj_set_size(back_btn, 76, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 12, 14);
    lv_obj_add_event_cb(back_btn, back_event_cb, LV_EVENT_CLICKED, RT_NULL);

    lv_obj_t *title = hs_ui_label_create(state->root, "Huangshan UI Lab",
                                         FONT_BIGL, HS_UI_COLOR_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    state->status_badge = hs_ui_status_badge_create(state->root, "AI READY", HS_UI_STATUS_OK);
    lv_obj_set_size(state->status_badge, 86, 28);
    lv_obj_align(state->status_badge, LV_ALIGN_TOP_RIGHT, -12, 20);

    lv_obj_t *subtitle = hs_ui_label_create(state->root, "LVGL theme + components + review loop",
                                            FONT_SMALL, HS_UI_COLOR_MUTED);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 58);
}

static void create_metrics(huangshan_ui_lab_t *state)
{
    lv_obj_t *tile;

    tile = hs_ui_value_tile_create(state->root, "Panel", "390", "px", HS_UI_STATUS_OK);
    lv_obj_align(tile, LV_ALIGN_TOP_LEFT, 18, 92);

    tile = hs_ui_value_tile_create(state->root, "Touch", "44", "min", HS_UI_STATUS_OK);
    lv_obj_align(tile, LV_ALIGN_TOP_MID, 0, 92);

    tile = hs_ui_value_tile_create(state->root, "Budget", "Low", "fx", HS_UI_STATUS_WARN);
    lv_obj_align(tile, LV_ALIGN_TOP_RIGHT, -18, 92);
}

static void create_chart_card(huangshan_ui_lab_t *state)
{
    lv_obj_t *card = hs_ui_card_create(state->root);
    lv_obj_set_size(card, 354, 132);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 196);

    lv_obj_t *title = hs_ui_label_create(card, "Bounded Live Region",
                                         FONT_SMALL, HS_UI_COLOR_MUTED);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    state->tick_label = hs_ui_label_create(card, "Tick 0", FONT_SMALL, HS_UI_COLOR_WARN);
    lv_obj_align(state->tick_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    state->chart = lv_chart_create(card);
    lv_obj_set_size(state->chart, 326, 76);
    lv_obj_align(state->chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(state->chart, lv_color_hex(0x0B1220), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(state->chart, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(state->chart, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(state->chart, lv_color_hex(0x2A3A54), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(state->chart, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_chart_set_type(state->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(state->chart, HS_UI_TREND_POINTS);
    lv_chart_set_range(state->chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    state->series = lv_chart_add_series(state->chart, HS_UI_COLOR_ACCENT,
                                        LV_CHART_AXIS_PRIMARY_Y);

    for (int i = 0; i < HS_UI_TREND_POINTS; i++)
    {
        lv_chart_set_next_value(state->chart, state->series, 34 + ((i * 9) % 32));
    }
}

static void create_footer(huangshan_ui_lab_t *state)
{
    lv_obj_t *review = hs_ui_action_button_create(state->root, "Review", false);
    lv_obj_align(review, LV_ALIGN_BOTTOM_LEFT, 18, -54);

    lv_obj_t *limit = hs_ui_action_button_create(state->root, "Budget", false);
    lv_obj_align(limit, LV_ALIGN_BOTTOM_MID, 0, -54);

    lv_obj_t *danger = hs_ui_action_button_create(state->root, "Alert", true);
    lv_obj_align(danger, LV_ALIGN_BOTTOM_RIGHT, -18, -54);

    state->touch_label = hs_ui_label_create(state->root, "Touch 0", FONT_SMALL, HS_UI_COLOR_TEXT);
    lv_obj_align(state->touch_label, LV_ALIGN_BOTTOM_LEFT, 22, -18);

    lv_obj_t *hint = hs_ui_label_create(state->root, "Component-first LVGL page. Tap blank area.",
                                        FONT_SMALL, HS_UI_COLOR_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_RIGHT, -18, -18);
}

static void on_start(void)
{
    rt_memset(&g_ui_lab, 0, sizeof(g_ui_lab));
    hs_ui_theme_init();

    g_ui_lab.root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(g_ui_lab.root);
    lv_obj_add_style(g_ui_lab.root, &hs_ui_style_screen, 0);
    lv_obj_set_size(g_ui_lab.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_align(g_ui_lab.root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_ui_lab.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_ui_lab.root, root_event_cb, LV_EVENT_CLICKED, &g_ui_lab);

    create_header(&g_ui_lab);
    create_metrics(&g_ui_lab);
    create_chart_card(&g_ui_lab);
    create_footer(&g_ui_lab);

    g_ui_lab.timer = lv_timer_create(timer_cb, 1000, &g_ui_lab);
    rt_kprintf("[Huangshan_UI_Lab] start\n");
}

static void on_resume(void)
{
    rt_kprintf("[Huangshan_UI_Lab] resume\n");
}

static void on_pause(void)
{
    rt_kprintf("[Huangshan_UI_Lab] pause\n");
}

static void on_stop(void)
{
    if (g_ui_lab.timer)
    {
        lv_timer_del(g_ui_lab.timer);
        g_ui_lab.timer = RT_NULL;
    }

    if (g_ui_lab.root)
    {
        lv_obj_del(g_ui_lab.root);
        g_ui_lab.root = RT_NULL;
    }

    rt_kprintf("[Huangshan_UI_Lab] stop\n");
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
    rt_kprintf("[Huangshan_UI_Lab] registered\n");
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(huangshan_ui_lab), LV_EXT_IMG_GET(img_LiChuang), APP_ID, app_main);
