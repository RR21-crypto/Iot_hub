# IOT_hub

A smartphone-style UI template for the **Waveshare ESP32-S3-Touch-LCD-4** (480×480 touch screen),
built with **ESP-IDF v6.1** and **LVGL 9.5**. Meant as a base for future IoT projects: apps, services
and a state machine are separated so new features can be added without touching the rest.

## Features

- **Home screen** with app icons, **status bar** (clock, WiFi, Bluetooth) and a **quick panel**
  (tap or swipe down on the status bar) with WiFi and Bluetooth toggles
- **Apps**
  - **Gallery**: JPG photos and animated GIFs from flash, swipe to browse
  - **Clock**: clock, stopwatch with laps, countdown timer
  - **Notes**: write notes with the on-screen keyboard, kept across reboots and re-flashing
  - **Weather**: current weather + 3-day forecast ([Open-Meteo](https://open-meteo.com), no API key),
    automatic location (IP based) or city search
  - **Settings**: WiFi setup, firmware update, I2C device scan, memory/storage info, FPS overlay
- **WiFi** setup on the screen; time synced over NTP and kept in the on-board RTC (PCF85063)
- **Firmware update over WiFi (OTA)** from a web browser, protected by a 4-digit code shown on
  the screen, with automatic rollback if the new firmware does not start
- **Bluetooth Low Energy**: the board shows up as "Anna Hub"; read its info or send text to the
  screen from a phone (e.g. with nRF Connect)

## Hardware

- Waveshare ESP32-S3-Touch-LCD-4, **board revision V3.0** (TCA9554 IO expander at I2C 0x20).
  V4.0 boards use a different IO expander and need Waveshare's newer BSP.
- ESP32-S3 with 16 MB flash and 8 MB octal PSRAM
- ST7701 480×480 RGB panel, GT911 touch, PCF85063 RTC

## Build and flash

1. Install [ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/).
2. Build and flash with a USB cable (the first flash must be by cable):
   ```
   idf.py set-target esp32s3
   idf.py build flash monitor
   ```
3. On the board: **Settings > WiFi > Scan**, pick your network and enter the password (2.4 GHz only).

### Updating over WiFi

1. Bump `PROJECT_VER` in the top-level `CMakeLists.txt` and run `idf.py build`.
2. Open the address shown in **Settings > Firmware update** (e.g. `http://192.168.1.x/`) from a
   computer on the same network.
3. Choose `build/anna_display.bin`, type the 4-digit code from the screen and press **Upload**.

OTA updates the program only. Gallery pictures are written by a cable flash.

## Project layout

```
main/
  main.c, main.h        boot sequence and app-wide settings (time zone, default weather city...)
  core/                 app_sm: event-driven, table-based state machine; app_registry: list of apps
  ui/                   theme, status bar, quick panel, home screen, toasts
  apps/<name>/          one folder per app
  services/             storage, time, WiFi, OTA, weather, Bluetooth (each runs on its own,
                        reports to the state machine with app_sm_post())
  i2c/, rtc/, assets/   I2C scanner, RTC helpers, generated weather icons
components/
  esp32_s3_touch_lcd_4/ Waveshare BSP 1.1.0 (V3.0 board), patched for ESP-IDF v6
  pcf85063/             RTC driver
data/gallery/           your photos/GIFs (not in git, see the README there)
tools/                  image conversion and icon generation scripts (Python + Pillow)
```

### Adding an app

1. Create `main/apps/<name>/<name>_app.c/.h` defining a `const app_desc_t <name>_app`
   (name, icon, `create`, and optionally `destroy`, `on_tick`, `on_event`).
2. Add it to the `apps[]` table in `main/core/app_registry.c`.
3. Add the `.c` file to `main/CMakeLists.txt`.

App callbacks run with the display lock held, so they can use LVGL directly. Long work (network,
sensors) belongs in a service task that posts an event when it is done.

## Flash layout

| Partition | Size | Content |
|---|---|---|
| ota_0 / ota_1 | 3 MB each | firmware (two slots for OTA) |
| gallery | 6 MB | SPIFFS image built from `data/gallery` |
| user | ~4 MB | SPIFFS for notes, never overwritten by flashing |

## Notes

- The SD card slot cannot be used: its pins are shared with the LCD in this BSP.
- The ESP32-S3 has Bluetooth Low Energy only (no Classic Bluetooth / file sharing to phones).
- If WiFi works but the board cannot be reached from a laptop, check that both are on the same
  access point (some WiFi extenders isolate clients).

## Licenses

The Waveshare BSP in `components/esp32_s3_touch_lcd_4` keeps its own license (see the LICENSE file
in that folder). ESP-IDF, LVGL and the managed components are under their respective licenses.
