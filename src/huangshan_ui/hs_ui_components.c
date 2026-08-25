#include <rtthread.h>
#include "hs_ui_components.h"
#include "lv_ext_resource_manager.h"
#include "lv_ex_data.h"

lv_obj_t *hs_ui_label_create(lv_obj_t *parent, const char *text, uint16_t font_size,
                             lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_ext_set_local_font(label, font_size, color);
    return label;
}

lv_obj_t *hs_ui_card_create(lv_obj_t *parent)
{
    lv_obj_t *card;
    hs_ui_theme_init();
    card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &hs_ui_style_card, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *hs_ui_header_create(lv_obj_t *parent, const char *title, const char *subtitle)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_t *title_label;
    lv_obj_t *subtitle_label;
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, 330, 68);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    title_label = hs_ui_label_create(header, title, FONT_BIGL, HS_UI_COLOR_TEXT);
    lv_obj_set_width(title_label, 330);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);
    subtitle_label = hs_ui_label_create(header, subtitle, FONT_SMALL, HS_UI_COLOR_MUTED);
    lv_obj_set_width(subtitle_label, 330);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_LEFT, 0, 36);
    return header;
}

lv_obj_t *hs_ui_metric_create(lv_obj_t *parent, const char *title, const char *value,
                              hs_ui_status_t status, lv_obj_t **value_label)
{
    lv_obj_t *card = hs_ui_card_create(parent);
    lv_obj_t *title_label;
    lv_obj_t *primary;
    lv_obj_t *dot;
    lv_obj_set_size(card, 104, 88);

    title_label = hs_ui_label_create(card, title, FONT_SMALL, HS_UI_COLOR_MUTED);
    lv_obj_set_width(title_label, 60);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);
    primary = hs_ui_label_create(card, value, FONT_BIGL, HS_UI_COLOR_TEXT);
    lv_obj_set_width(primary, 80);
    lv_obj_align(primary, LV_ALIGN_BOTTOM_LEFT, 0, -2);

    dot = lv_obj_create(card);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dot, hs_ui_status_color(status), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, 0, 3);
    if (value_label) *value_label = primary;
    return card;
}

lv_obj_t *hs_ui_value_tile_create(lv_obj_t *parent, const char *title,
                                  const char *value, const char *unit,
                                  hs_ui_status_t status)
{
    lv_obj_t *primary = NULL;
    lv_obj_t *card = hs_ui_metric_create(parent, title, value, status, &primary);
    lv_obj_t *unit_label = hs_ui_label_create(card, unit, FONT_SMALL, HS_UI_COLOR_MUTED);
    lv_obj_align_to(unit_label, primary, LV_ALIGN_OUT_RIGHT_BOTTOM, HS_UI_SPACE_1, -2);
    return card;
}

lv_obj_t *hs_ui_status_badge_create(lv_obj_t *parent, const char *text,
                                    hs_ui_status_t status)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_t *label;
    lv_obj_remove_style_all(badge);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(badge, 96, 28);

    if (HS_UI_STATUS_WARN == status) lv_obj_add_style(badge, &hs_ui_style_badge_warn, 0);
    else if (HS_UI_STATUS_DANGER == status) lv_obj_add_style(badge, &hs_ui_style_badge_danger, 0);
    else lv_obj_add_style(badge, &hs_ui_style_badge_ok, 0);

    label = hs_ui_label_create(badge, text, FONT_SMALL,
                               HS_UI_STATUS_WARN == status ? lv_color_hex(0x1F1604) :
                               HS_UI_STATUS_DANGER == status ? HS_UI_COLOR_TEXT : HS_UI_COLOR_INK);
    lv_obj_center(label);
    return badge;
}

lv_obj_t *hs_ui_progress_create(lv_obj_t *parent, const char *label, int value,
                                hs_ui_status_t status, lv_obj_t **bar_out)
{
    lv_obj_t *container = hs_ui_card_create(parent);
    lv_obj_t *caption;
    lv_obj_t *value_label;
    lv_obj_t *bar;
    char text[12];
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    lv_obj_set_size(container, 330, 70);
    caption = hs_ui_label_create(container, label, FONT_SMALL, HS_UI_COLOR_MUTED);
    lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 0, 0);
    rt_snprintf(text, sizeof(text), "%d%%", value);
    value_label = hs_ui_label_create(container, text, FONT_SMALL, HS_UI_COLOR_TEXT);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    bar = lv_bar_create(container);
    lv_obj_set_size(bar, 306, 12);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, hs_ui_status_color(status), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    if (bar_out) *bar_out = bar;
    return container;
}

lv_obj_t *hs_ui_action_button_create(lv_obj_t *parent, const char *text, bool danger)
{
    lv_obj_t *btn;
    lv_obj_t *label;
    hs_ui_theme_init();
    btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, danger ? &hs_ui_style_button_danger : &hs_ui_style_button, 0);
    lv_obj_set_size(btn, 108, 48);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    label = hs_ui_label_create(btn, text, FONT_SMALL,
                               danger ? HS_UI_COLOR_TEXT : HS_UI_COLOR_INK);
    lv_obj_center(label);
    return btn;
}
