local root = lv_scr_act()
lv_obj_clean(root)
lv_obj_set_style_bg_color(root, 0x07111f)
lv_obj_set_style_text_color(root, 0xf8fafc)

vibe_imu_lab("Gyro Dial")
print("[imu_lab] attitude dial ready")
