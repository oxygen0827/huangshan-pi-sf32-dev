#ifndef HS_UI_COMPONENTS_H
#define HS_UI_COMPONENTS_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "hs_ui_theme.h"

lv_obj_t *hs_ui_label_create(lv_obj_t *parent, const char *text, uint16_t font_size,
                             lv_color_t color);
lv_obj_t *hs_ui_card_create(lv_obj_t *parent);
lv_obj_t *hs_ui_header_create(lv_obj_t *parent, const char *title, const char *subtitle);
lv_obj_t *hs_ui_value_tile_create(lv_obj_t *parent, const char *title,
                                  const char *value, const char *unit,
                                  hs_ui_status_t status);
lv_obj_t *hs_ui_metric_create(lv_obj_t *parent, const char *title, const char *value,
                              hs_ui_status_t status, lv_obj_t **value_label);
lv_obj_t *hs_ui_status_badge_create(lv_obj_t *parent, const char *text,
                                    hs_ui_status_t status);
lv_obj_t *hs_ui_progress_create(lv_obj_t *parent, const char *label, int value,
                                hs_ui_status_t status, lv_obj_t **bar_out);
lv_obj_t *hs_ui_action_button_create(lv_obj_t *parent, const char *text, bool danger);

#endif
