local root = lv_scr_act()
lv_obj_clean(root)
lv_obj_set_style_bg_color(root, 0x050b14)
lv_obj_set_style_text_color(root, 0xf8fafc)

vibe_jump_game("跳一跳")
print("[jump_jump] native game requested")
