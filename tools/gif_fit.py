"""Resize an existing animated GIF to fit the 480x480 LCD, keeping frame timing.

Usage: python gif_fit.py <input .gif> <output .gif> [max size]
The aspect ratio is kept (a wide GIF ends up letterboxed on the square screen).
"""
import sys
from PIL import Image, ImageSequence

src, dst = sys.argv[1], sys.argv[2]
max_size = int(sys.argv[3]) if len(sys.argv) > 3 else 480

im = Image.open(src)
scale = min(max_size / im.width, max_size / im.height)
size = (round(im.width * scale), round(im.height * scale))

frames, durations = [], []
for frame in ImageSequence.Iterator(im):
    # Composite each frame to full RGB first so partial/transparent frames are handled
    rgb = frame.convert("RGB").resize(size, Image.LANCZOS)
    frames.append(rgb.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE))
    durations.append(frame.info.get("duration", 100))

frames[0].save(dst, save_all=True, append_images=frames[1:], duration=durations, loop=0, disposal=1)
print(f"{dst}: {size[0]}x{size[1]}, {len(frames)} frames, {sum(durations)} ms")
