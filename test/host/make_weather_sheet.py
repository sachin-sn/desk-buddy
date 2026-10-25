"""Contact sheet of the weather icons from render_weather, shown 3x on the dark screen colour."""
import sys
from PIL import Image, ImageDraw

out = sys.argv[1] if len(sys.argv) > 1 else "out"
names = ["sun", "moon", "partly_day", "partly_night", "cloud", "fog", "drizzle", "rain", "snow", "storm"]
cols, zoom, pad = 5, 3, 12
w, h = 104 * zoom, 80 * zoom
rows = (len(names) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + pad + 18) + pad), (32, 34, 40))
d = ImageDraw.Draw(sheet)
for i, n in enumerate(names):
    img = Image.open(f"{out}/wx_{n}.ppm").convert("RGB").resize((w, h), Image.LANCZOS)
    x, y = pad + (i % cols) * (w + pad), pad + (i // cols) * (h + pad + 18)
    sheet.paste(img, (x, y))
    d.text((x + 4, y + h + 2), n.replace("_", " "), fill=(200, 200, 205))
sheet.save(f"{out}/weather_icons.png")
print("wrote", f"{out}/weather_icons.png")
