# mk-clock-adult 2.3.22

Bluetooth dual-mono output retains about 1.51 dB of peak headroom (0.42L + 0.42R). Bluetooth media display is intentionally Title-only for stable iPhone behavior. busctl octal-escaped UTF-8 is decoded before IPC, and common accented Latin characters are folded to display-safe ASCII only at the OLED presentation layer (for example `Noël` displays as `Noel`).

Banana Pi M2 Zero adult clock application for the `bpi-zero-clock 1.0.3` hardware image.

This release retains the working 2.3.16 Bluetooth pairing/audio behavior and removes the unused Boot partition, Boot filesystem and Boot mount rows from the System → Storage GUI. The lower-level diagnostic fields remain available internally for support use.

Bluetooth pairing/control, pairing-only rapid polling, dual-mono Bluetooth playback, conditional `sinc-fastest` resampling, and local MP3/alarm playback are unchanged from 2.3.16.

## Weather panel form behavior

Weather panel controls are mode-aware. Only configuration fields used by the selected source are shown; unrelated Hours ahead / Specific time fields are hidden.
