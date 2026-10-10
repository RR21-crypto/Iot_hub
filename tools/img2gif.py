"""Make a small looping GIF ("breathing" zoom) from a still image for the LCD.

Usage: python img2gif.py <input image> <output .gif> [size] [frames]
The image is center-cropped to a square, then each frame zooms in slightly and back out.
Smaller sizes decode faster on the ESP32-S3 (360 is a good balance for the 480x480 LCD).
"""
import math
import sys
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
size = int(sys.argv[3]) if len(sys.argv) > 3 else 360
frames = int(sys.argv[4]) if len(sys.argv) > 4 else 16
max_zoom = 0.10       # zoom in up to 10%
frame_ms = 80

img = Image.open(src).convert("RGB")
w, h = img.size
side = min(w, h)
left, top = (w - side) // 2, (h - side) // 2
base = img.crop((left, top, left + side, top + side))

# One shared palette for all frames: smaller file and no color flicker between frames
palette_img = base.resize((size, size), Image.LANCZOS).quantize(colors=256, method=Image.Quantize.MEDIANCUT)

out = []
for i in range(frames):
    zoom = 1 + max_zoom * (1 - math.cos(2 * math.pi * i / frames)) / 2
    crop = side / zoom
    off = (side - crop) / 2
    frame = base.resize((size, size), Image.LANCZOS, box=(off, off, off + crop, off + crop))
    out.append(frame.quantize(palette=palette_img, dither=Image.Dither.NONE))

out[0].save(dst, save_all=True, append_images=out[1:], duration=frame_ms, loop=0, optimize=False)
print(f"{dst}: {size}x{size}, {frames} frames")
