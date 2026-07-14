local root = lv_scr_act()
lv_obj_clean(root)
lv_obj_set_style_bg_color(root, 0x050b14)
lv_obj_set_style_text_color(root, 0xf8fafc)

vibe_breakout_game("Neon Breakout")
print("[breakout] touch game ready")
