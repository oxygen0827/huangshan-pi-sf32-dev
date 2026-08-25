from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
SPRITES = ROOT / "scripts/runtime_apps/thunder_wing/assets/sprites"
OUTPUT = ROOT / "scripts/runtime_apps/thunder_wing/assets/screenshot.png"


def sprite(name: str) -> Image.Image:
    return Image.open(SPRITES / f"{name}.png").convert("RGBA")


canvas = Image.new("RGBA", (320, 220), (4, 9, 22, 255))
draw = ImageDraw.Draw(canvas)
for y in range(220):
    color = (4 + y // 28, 9 + y // 22, 22 + y // 9, 255)
    draw.line((0, y, 319, y), fill=color)

draw.rounded_rectangle((38, 18, 282, 208), radius=8, fill=(3, 8, 20, 255),
                       outline=(36, 64, 95, 255), width=2)
for index in range(28):
    x = 46 + (index * 53) % 228
    y = 27 + (index * 37) % 171
    radius = 2 if index % 7 == 0 else 1
    draw.ellipse((x - radius, y - radius, x + radius, y + radius),
                 fill=(147, 197, 253, 220 if radius == 2 else 150))

canvas.alpha_composite(sprite("boss"), (112, 23))
canvas.alpha_composite(sprite("enemy_scout"), (70, 88))
canvas.alpha_composite(sprite("enemy_elite"), (207, 82))
canvas.alpha_composite(sprite("enemy_scout"), (130, 108))
canvas.alpha_composite(sprite("player"), (136, 151))

for x, y in ((158, 136), (158, 128), (145, 144), (173, 144)):
    draw.rounded_rectangle((x, y, x + 3, y + 10), radius=1, fill=(94, 234, 212, 255))
for x, y in ((88, 134), (226, 126), (191, 147)):
    draw.ellipse((x, y, x + 6, y + 9), fill=(255, 64, 88, 255))

draw.rounded_rectangle((53, 28, 102, 34), radius=3, fill=(56, 189, 248, 220))
draw.rounded_rectangle((108, 28, 150, 34), radius=3, fill=(94, 234, 212, 190))
draw.rounded_rectangle((156, 28, 199, 34), radius=3, fill=(251, 191, 36, 210))
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
canvas.convert("RGB").save(OUTPUT, optimize=True)
