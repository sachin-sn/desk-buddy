# PPM renders -> one contact sheet (and, with --thumbs DIR, the round thumbnails for the web editor).
import os, sys
from PIL import Image, ImageDraw

out = sys.argv[1]
thumbs = sys.argv[sys.argv.index("--thumbs") + 1] if "--thumbs" in sys.argv else None
names = sorted(f[3:-4] for f in os.listdir(out) if f.startswith("lv_") and f.endswith(".ppm"))
size, pad, cols = 240, 10, 5
rows = (len(names) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (size + pad) + pad, rows * (size + pad + 16) + pad), (40, 42, 48))
mask = Image.new("L", (size, size), 0)
ImageDraw.Draw(mask).ellipse((0, 0, size - 1, size - 1), fill=255)
d = ImageDraw.Draw(sheet)
for i, n in enumerate(names):
    im = Image.open(f"{out}/lv_{n}.ppm").convert("RGB")
    ring = Image.new("RGB", (size, size), (14, 14, 16))
    ring.paste(im, (0, 0), mask)
    x = pad + (i % cols) * (size + pad)
    y = pad + (i // cols) * (size + pad + 16)
    sheet.paste(ring, (x, y))
    d.text((x + 4, y + size + 2), n, fill=(200, 200, 205))
    ring.save(f"{out}/{n}.png")
sheet.save(f"{out}/faces_sheet.png")
print("sheet:", len(names), "pictures")

if thumbs:                      # the picker shows one round picture per face (the *_bare ones are not used)
    os.makedirs(thumbs, exist_ok=True)
    for key in ["classic", "analog", "minimal", "tacho", "roll", "photo"]:
        im = Image.open(f"{out}/lv_{key}.ppm").convert("RGB")
        th = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        th.paste(im.convert("RGBA"), (0, 0), mask)
        th = th.resize((160, 160), Image.LANCZOS)
        th.save(f"{thumbs}/{key}.png", optimize=True)
    print("thumbnails written to", thumbs)
