# Gallery pictures

Files in this folder are packed into the `gallery` flash partition on every cable flash
(`idf.py flash`) and shown by the Gallery app. They are not tracked by git.

- Photos: `python tools/gallery_prep.py my_photo.jpg` (scales to fit 480x400, saves a baseline JPEG here)
- GIFs: `python tools/gif_fit.py my_anim.gif data/gallery/my_anim.gif 480`
- File names must be short (SPIFFS limit: 32 characters including the path).
- The partition is 6 MB.

Note: this README itself is also copied into the partition; the Gallery app ignores it.
