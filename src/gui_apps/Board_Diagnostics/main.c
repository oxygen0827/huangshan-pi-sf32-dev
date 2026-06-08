#include <rtthread.h>
#include <rtdevice.h>
#include "lvgl.h"
#include "gui_app_fwk.h"
#include "lv_ext_resource_manager.h"
#include "lv_ex_data.h"

#define APP_ID "board_diag"

#ifndef BSP_KEY1_PIN
    #define BSP_KEY1_PIN 34
#endif

#ifndef BSP_KEY2_PIN
    #define BSP_KEY2_PIN 43
#endif

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *uptime_label;
    lv_obj_t *tick_label;
    lv_obj_t *touch_label;
    lv_obj_t *coord_label;
    lv_obj_t *key1_label;
    lv_obj_t *key2_label;
    lv_timer_t *timer;
    uint32_t seconds;
    uint32_t tick_count;
    uint32_t touch_count;
    int last_key1_level;
    int last_key2_level;
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

static const char *level_to_text(int level)
{
    return level ? "HIGH" : "LOW";
}

static int key1_is_active(rt_base_t pin_level)
{
#ifdef BSP_KEY1_ACTIVE_HIGH
    return pin_level ? 1 : 0;
#else
    return pin_level ? 0 : 1;
#endif
}

static int key2_is_active(rt_base_t pin_level)
{
#ifdef BSP_KEY2_ACTIVE_HIGH
    return pin_level ? 1 : 0;
#else
    return pin_level ? 0 : 1;
#endif
}

static void update_signal_labels(board_diag_t *state)
{
    int key1_level = rt_pin_read(BSP_KEY1_PIN);
    int key2_level = rt_pin_read(BSP_KEY2_PIN);

    if (state->key1_label)
    {
        lv_label_set_text_fmt(state->key1_label, "%s %s",
                              level_to_text(key1_level), key1_is_active(key1_level) ? "PRESSED" : "IDLE");
    }

    if (state->key2_label)
    {
        lv_label_set_text_fmt(state->key2_label, "%s %s",
                              level_to_text(key2_level), key2_is_active(key2_level) ? "PRESSED" : "IDLE");
    }

    if (state->last_key1_level != key1_level)
    {
        state->last_key1_level = key1_level;
        rt_kprintf("[Board_Diagnostics] KEY1 GPIO%d %s %s\n", BSP_KEY1_PIN,
                   level_to_text(key1_level), key1_is_active(key1_level) ? "PRESSED" : "IDLE");
    }

    if (state->last_key2_level != key2_level)
    {
        state->last_key2_level = key2_level;
        rt_kprintf("[Board_Diagnostics] KEY2 GPIO%d %s %s\n", BSP_KEY2_PIN,
                   level_to_text(key2_level), key2_is_active(key2_level) ? "PRESSED" : "IDLE");
    }
}

static void create_color_block(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, uint32_t color)
{
    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_set_size(block, 78, 42);
    lv_obj_align(block, LV_ALIGN_TOP_LEFT, x, y);
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

    update_signal_labels(state);
}

static void root_event_cb(lv_event_t *event)
{
    board_diag_t *state = (board_diag_t *)lv_event_get_user_data(event);

    if (LV_EVENT_CLICKED == lv_event_get_code(event))
    {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t point = {0, 0};

        state->touch_count++;
        if (indev)
        {
            lv_indev_get_point(indev, &point);
        }

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

static void init_signal_pins(void)
{
    rt_pin_mode(BSP_KEY1_PIN, PIN_MODE_INPUT);
    rt_pin_mode(BSP_KEY2_PIN, PIN_MODE_INPUT);
}

static void on_start(void)
{
    rt_memset(&g_board_diag, 0, sizeof(g_board_diag));
    g_board_diag.last_key1_level = -1;
    g_board_diag.last_key2_level = -1;
    init_signal_pins();

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
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *subtitle = create_label(g_board_diag.root, "SF32LB52 / CO5300 / FT6146",
                                      FONT_SMALL, lv_color_hex(0x93c5fd));
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 58);

    g_board_diag.uptime_label = create_label(g_board_diag.root, "Uptime 0 s", FONT_SMALL,
                                             lv_color_hex(0xfacc15));
    lv_obj_align(g_board_diag.uptime_label, LV_ALIGN_TOP_MID, 0, 84);

    create_metric_card(g_board_diag.root, 22, 118, "Touch Count", "0", &g_board_diag.touch_label);
    create_metric_card(g_board_diag.root, 198, 118, "Last XY", "--,--", &g_board_diag.coord_label);
    create_metric_card(g_board_diag.root, 22, 202, "KEY1 GPIO34", "--", &g_board_diag.key1_label);
    create_metric_card(g_board_diag.root, 198, 202, "KEY2 GPIO43", "--", &g_board_diag.key2_label);

    create_metric_card(g_board_diag.root, 22, 286, "LVGL Tick", "0", &g_board_diag.tick_label);
    create_metric_card(g_board_diag.root, 198, 286, "Panel", "390 x 450", RT_NULL);

    create_color_block(g_board_diag.root, 22, 370, 0xff3030);
    create_color_block(g_board_diag.root, 112, 370, 0x20d060);
    create_color_block(g_board_diag.root, 202, 370, 0x3078ff);
    create_color_block(g_board_diag.root, 292, 370, 0xffffff);

    update_signal_labels(&g_board_diag);

    lv_obj_t *hint = create_label(g_board_diag.root, "Tap blank area for touch. Press KEY1 or KEY2.",
                                  FONT_SMALL, lv_color_hex(0xa8b3bd));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);

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
