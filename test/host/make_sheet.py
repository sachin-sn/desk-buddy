"""Turn the PPM frames from render_eyes into one PNG contact sheet, masked to the round screen."""
import sys
from PIL import Image, ImageDraw, ImageFont

out = sys.argv[1] if len(sys.argv) > 1 else "out"
names = ["mood_normal", "mood_happy", "mood_tired", "mood_angry", "mood_surprised", "mood_sleep",
         "look_right_up", "blink_mid"]
cols, size, pad = 4, 240, 12
rows = (len(names) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (size + pad) + pad, rows * (size + pad + 18) + pad), (32, 34, 40))
mask = Image.new("L", (size, size), 0)
ImageDraw.Draw(mask).ellipse((0, 0, size - 1, size - 1), fill=255)
d = ImageDraw.Draw(sheet)
for i, n in enumerate(names):
    img = Image.open(f"{out}/{n}.ppm").convert("RGB")
    ring = Image.new("RGB", (size, size), (14, 14, 16))
    ring.paste(img, (0, 0), mask)
    x = pad + (i % cols) * (size + pad)
    y = pad + (i // cols) * (size + pad + 18)
    sheet.paste(ring, (x, y))
    d.text((x + 4, y + size + 2), n.replace("_", " "), fill=(200, 200, 205))
sheet.save(f"{out}/eyes_sheet.png")
print("wrote", f"{out}/eyes_sheet.png")
