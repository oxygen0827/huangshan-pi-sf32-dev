local root = lv_scr_act()
lv_obj_clean(root)
lv_obj_set_style_bg_color(root, 0x090d12)
lv_obj_set_style_text_color(root, 0xf8fafc)

vibe_pomodoro("Focus Garden", 25, 5)
print("[pomodoro] interactive timer ready")
