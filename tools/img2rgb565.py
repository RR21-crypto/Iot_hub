"""Convert an image to raw RGB565 (little-endian) for the 480x480 LCD.

Usage: python img2rgb565.py <input image> <output .rgb565> [size]
The image is center-cropped to a square, then resized.
"""
import sys
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
size = int(sys.argv[3]) if len(sys.argv) > 3 else 480

img = Image.open(src).convert("RGB")
w, h = img.size
side = min(w, h)
left, top = (w - side) // 2, (h - side) // 2
img = img.crop((left, top, left + side, top + side)).resize((size, size), Image.LANCZOS)

out = bytearray()
for r, g, b in img.getdata():
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    out += v.to_bytes(2, "little")

with open(dst, "wb") as f:
    f.write(out)
img.save(dst + ".preview.png")
print(f"{dst}: {size}x{size}, {len(out)} bytes")
