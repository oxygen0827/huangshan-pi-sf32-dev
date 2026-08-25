local root = lv_scr_act()
lv_obj_clean(root)
lv_obj_set_style_bg_color(root, 0x0f172a)
lv_obj_set_style_text_color(root, 0xf8fafc)

vibe_2048_game("2048")
print("[game_2048] native playable 2048 requested")
