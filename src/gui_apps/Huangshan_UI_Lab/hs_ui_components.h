#ifndef HS_UI_COMPONENTS_H
#define HS_UI_COMPONENTS_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "hs_ui_theme.h"

lv_obj_t *hs_ui_label_create(lv_obj_t *parent, const char *text, uint16_t font_size,
                             lv_color_t color);
lv_obj_t *hs_ui_card_create(lv_obj_t *parent);
lv_obj_t *hs_ui_value_tile_create(lv_obj_t *parent, const char *title,
                                  const char *value, const char *unit,
                                  hs_ui_status_t status);
lv_obj_t *hs_ui_status_badge_create(lv_obj_t *parent, const char *text,
                                    hs_ui_status_t status);
lv_obj_t *hs_ui_action_button_create(lv_obj_t *parent, const char *text, bool danger);

#endif
