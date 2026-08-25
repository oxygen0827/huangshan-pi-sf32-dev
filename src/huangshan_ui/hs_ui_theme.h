#ifndef HS_UI_THEME_H
#define HS_UI_THEME_H

#include "lvgl.h"

#define HS_UI_COLOR_BG       lv_color_hex(0x080D14)
#define HS_UI_COLOR_PANEL    lv_color_hex(0x172033)
#define HS_UI_COLOR_PANEL_2  lv_color_hex(0x22314A)
#define HS_UI_COLOR_TEXT     lv_color_hex(0xF8FAFC)
#define HS_UI_COLOR_MUTED    lv_color_hex(0x9FB2C8)
#define HS_UI_COLOR_ACCENT   lv_color_hex(0x22D3EE)
#define HS_UI_COLOR_OK       lv_color_hex(0x34D399)
#define HS_UI_COLOR_WARN     lv_color_hex(0xFACC15)
#define HS_UI_COLOR_DANGER   lv_color_hex(0xFB7185)
#define HS_UI_COLOR_INK      lv_color_hex(0x08111A)

#define HS_UI_SPACE_1 4
#define HS_UI_SPACE_2 8
#define HS_UI_SPACE_3 12
#define HS_UI_SPACE_4 16
#define HS_UI_RADIUS  8

typedef enum
{
    HS_UI_STATUS_OK = 0,
    HS_UI_STATUS_WARN,
    HS_UI_STATUS_DANGER,
    HS_UI_STATUS_NEUTRAL
} hs_ui_status_t;

extern lv_style_t hs_ui_style_screen;
extern lv_style_t hs_ui_style_card;
extern lv_style_t hs_ui_style_card_alt;
extern lv_style_t hs_ui_style_button;
extern lv_style_t hs_ui_style_button_danger;
extern lv_style_t hs_ui_style_badge_ok;
extern lv_style_t hs_ui_style_badge_warn;
extern lv_style_t hs_ui_style_badge_danger;

void hs_ui_theme_init(void);
void hs_ui_screen_apply(lv_obj_t *screen);
lv_color_t hs_ui_status_color(hs_ui_status_t status);
hs_ui_status_t hs_ui_status_from_name(const char *name);

#endif
