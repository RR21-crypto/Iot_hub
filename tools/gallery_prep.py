"""Prepare photos for the Gallery app (data/gallery -> "gallery" SPIFFS partition).

Usage: python gallery_prep.py <image> [<image> ...] [--out data/gallery]
Each image is scaled down to fit 480x400 (the app area under the status/title bars)
and saved as a baseline JPEG, which the LVGL TJPGD decoder can read.
GIFs are not handled here: use gif_fit.py (max size 480) and copy the result into data/gallery.
SPIFFS file names are limited, so names are shortened to 24 characters.
"""
import argparse
import os
from PIL import Image

MAX_W, MAX_H = 480, 400
MAX_NAME = 24

parser = argparse.ArgumentParser()
parser.add_argument("images", nargs="+")
parser.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "data", "gallery"))
args = parser.parse_args()
os.makedirs(args.out, exist_ok=True)

for src in args.images:
    img = Image.open(src).convert("RGB")
    img.thumbnail((MAX_W, MAX_H), Image.LANCZOS)    # keeps aspect ratio, never enlarges
    name = os.path.splitext(os.path.basename(src))[0].lower().replace(" ", "_")[:MAX_NAME]
    dst = os.path.join(args.out, name + ".jpg")
    img.save(dst, "JPEG", quality=85, progressive=False, optimize=True)
    print(f"{dst}: {img.width}x{img.height}, {os.path.getsize(dst)} bytes")
