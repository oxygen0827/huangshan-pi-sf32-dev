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
  {"Audio Stage", 36},
  {"Lua 5.5 functions, tables and loops", 78},
  {"Bundled PCM WAV -> SiFli audio_server", 120}
}

for index, row in ipairs(rows) do
  labels[index] = add_line(row[1], row[2], palette[index])
end

local state = add_line("idle", 188, palette[4])
add_line("16 kHz / mono / 16-bit PCM", 232, 0x93c5fd)
add_line("K1 stops playback and returns home", 276, 0xc4b5fd)
vibe_audio_label(state, "state")

vibe_audio_volume(8)
vibe_audio_play("assets/chime.wav")
print("[audio_stage] full Lua VM ready; WAV playback requested")
