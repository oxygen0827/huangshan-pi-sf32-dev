#include "hs_ui_theme.h"

lv_style_t hs_ui_style_screen;
lv_style_t hs_ui_style_card;
lv_style_t hs_ui_style_card_alt;
lv_style_t hs_ui_style_button;
lv_style_t hs_ui_style_button_danger;
lv_style_t hs_ui_style_badge_ok;
lv_style_t hs_ui_style_badge_warn;
lv_style_t hs_ui_style_badge_danger;

static void init_panel_style(lv_style_t *style, lv_color_t bg, lv_color_t border)
{
    lv_style_init(style);
    lv_style_set_bg_color(style, bg);
    lv_style_set_bg_opa(style, LV_OPA_COVER);
    lv_style_set_border_width(style, 1);
    lv_style_set_border_color(style, border);
    lv_style_set_radius(style, HS_UI_RADIUS);
    lv_style_set_pad_all(style, HS_UI_SPACE_3);
}

static void init_button_style(lv_style_t *style, lv_color_t bg, lv_color_t text)
{
    lv_style_init(style);
    lv_style_set_bg_color(style, bg);
    lv_style_set_bg_opa(style, LV_OPA_COVER);
    lv_style_set_text_color(style, text);
    lv_style_set_border_width(style, 0);
    lv_style_set_radius(style, HS_UI_RADIUS);
    lv_style_set_pad_hor(style, HS_UI_SPACE_2);
    lv_style_set_pad_ver(style, HS_UI_SPACE_2);
}

static void init_badge_style(lv_style_t *style, lv_color_t bg, lv_color_t text)
{
    lv_style_init(style);
    lv_style_set_bg_color(style, bg);
    lv_style_set_bg_opa(style, LV_OPA_COVER);
    lv_style_set_text_color(style, text);
    lv_style_set_border_width(style, 0);
    lv_style_set_radius(style, HS_UI_RADIUS);
    lv_style_set_pad_hor(style, HS_UI_SPACE_2);
    lv_style_set_pad_ver(style, HS_UI_SPACE_1);
}

void hs_ui_theme_init(void)
{
    lv_style_init(&hs_ui_style_screen);
    lv_style_set_bg_color(&hs_ui_style_screen, HS_UI_COLOR_BG);
    lv_style_set_bg_opa(&hs_ui_style_screen, LV_OPA_COVER);
    lv_style_set_text_color(&hs_ui_style_screen, HS_UI_COLOR_TEXT);
    lv_style_set_pad_all(&hs_ui_style_screen, 0);
    lv_style_set_border_width(&hs_ui_style_screen, 0);
    lv_style_set_radius(&hs_ui_style_screen, 0);

    init_panel_style(&hs_ui_style_card, HS_UI_COLOR_PANEL, HS_UI_COLOR_PANEL_2);
    init_panel_style(&hs_ui_style_card_alt, lv_color_hex(0x0F172A), lv_color_hex(0x26354E));
    init_button_style(&hs_ui_style_button, HS_UI_COLOR_ACCENT, HS_UI_COLOR_INK);
    init_button_style(&hs_ui_style_button_danger, HS_UI_COLOR_DANGER, HS_UI_COLOR_TEXT);
    init_badge_style(&hs_ui_style_badge_ok, HS_UI_COLOR_OK, HS_UI_COLOR_INK);
    init_badge_style(&hs_ui_style_badge_warn, HS_UI_COLOR_WARN, lv_color_hex(0x1F1604));
    init_badge_style(&hs_ui_style_badge_danger, HS_UI_COLOR_DANGER, HS_UI_COLOR_TEXT);
}

lv_color_t hs_ui_status_color(hs_ui_status_t status)
{
    switch (status)
    {
    case HS_UI_STATUS_WARN:
        return HS_UI_COLOR_WARN;
    case HS_UI_STATUS_DANGER:
        return HS_UI_COLOR_DANGER;
    case HS_UI_STATUS_OK:
    default:
        return HS_UI_COLOR_OK;
    }
}
