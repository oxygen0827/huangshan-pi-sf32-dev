local root = lv_scr_act()
lv_obj_clean(root)
lv_obj_set_style_bg_color(root, 0x10151c)
lv_obj_set_style_text_color(root, 0xf8fafc)

local palette = {0x67e8f9, 0xcbd5e1, 0xfbbf24, 0x86efac}

local function add_line(text, y, color)
  local label = lv_label_create(root)
  lv_label_set_text(label, text)
  lv_obj_set_style_text_color(label, color)
  lv_obj_set_width(label, 330)
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 30, y)
  return label
end

local labels = {}
local rows = {
  {"Speaker Test", 36},
  {"External speaker required", 78},
  {"Connect it to the SPK port", 120}
}

for index, row in ipairs(rows) do
  labels[index] = add_line(row[1], row[2], palette[index])
end

local state = add_line("idle", 170, palette[4])
add_line("16 kHz / mono / 16-bit PCM", 216, 0x93c5fd)
vibe_audio_label(state, "state")

local play = lv_btn_create(root)
lv_obj_set_size(play, 150, 52)
lv_obj_align(play, LV_ALIGN_BOTTOM_LEFT, 28, -72)
lv_obj_set_style_bg_color(play, 0x15803d)
lv_obj_set_style_radius(play, 6)
local play_label = lv_label_create(play)
lv_label_set_text(play_label, "Play 1s")
lv_obj_set_style_text_color(play_label, 0xffffff)
lv_obj_center(play_label)
vibe_audio_tone_button(play)

local stop = lv_btn_create(root)
lv_obj_set_size(stop, 150, 52)
lv_obj_align(stop, LV_ALIGN_BOTTOM_RIGHT, -28, -72)
lv_obj_set_style_bg_color(stop, 0xdc2626)
lv_obj_set_style_radius(stop, 6)
local stop_label = lv_label_create(stop)
lv_label_set_text(stop_label, "Stop")
lv_obj_set_style_text_color(stop_label, 0xffffff)
lv_obj_center(stop_label)
vibe_audio_stop_button(stop)

add_line("K1 stops and returns home", 306, 0xc4b5fd)
vibe_audio_volume(8)
print("[audio_stage] waiting for explicit speaker test")
