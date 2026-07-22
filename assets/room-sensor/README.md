# ROOM sensor OLED sprites

This directory contains the four 32 x 32, 4-bit grayscale RAW sprites installed for the OLED runtime:

- `room-sensor-normal.raw`
- `room-sensor-waiting.raw`
- `room-sensor-stale.raw`
- `room-sensor-error.raw`

Each byte stores two pixels. Valid nibble levels are `0`, `5`, `10`, and `15`.

The browser renditions are maintained separately in `web/assets/icons/`. Keeping PNG files out of this runtime directory prevents duplicate source assets from being packaged and installed.
