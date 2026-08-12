## 2.3.22

- Simplifies Bluetooth media display to AVRCP Title only; separate Artist metadata is ignored.
- Decodes busctl octal-escaped UTF-8 bytes before the existing UTF-8-safe IPC path (for example `No\303\253l` becomes internal UTF-8 `Noël`).
- OLED metadata cleanup folds common accented Latin characters to display-safe ASCII, so `Noël` renders as `Noel` rather than an unsupported glyph or escaped byte sequence.
- Keeps the 2.3.20/2.3.21 bounded metadata refresh behavior, retaining the last non-empty Title across partial iPhone Track updates.
- Bluetooth audio level, pairing/connect behavior, resampling, weather GUI, local MP3/alarm audio and hardware behavior are unchanged from 2.3.21.

## 2.3.22

- Retunes Bluetooth speaker protection after 2.3.20 proved too quiet.
- Dual-mono Bluetooth coefficients increase from `0.35 + 0.35` to `0.42 + 0.42`, restoring output level while retaining approximately 1.51 dB of peak headroom versus 2.3.19.
- Keeps the 2.3.20 bounded metadata refresh burst and adds per-device partial Track merging: non-empty Title and Artist values are retained independently so iPhone AVRCP updates cannot erase the other field while it arrives separately.
- Bluetooth metadata continues through UTF-8-safe IPC truncation and the same OLED `oled_filter_metadata_text()` cleanup/formatter/marquee path used by local MP3 metadata.
- Bluetooth pairing/connect behavior, conditional `sinc-fastest` resampling, local MP3/alarm playback, weather GUI, and system GUI are unchanged.

## 2.3.20

- Keeps 2.3.19 Bluetooth pairing/connect behavior unchanged.
- Adds ~3.1 dB fixed Bluetooth peak headroom in the dual-mono route to reduce speaker rattle on loud transients with effectively zero added CPU cost.
- Local MP3/alarm audio is unchanged.
- Adds a bounded 5-second, 0.5-second-interval Bluetooth media refresh burst when A2DP becomes active or AVRCP track metadata changes, improving delayed/inconsistent iPhone title/artist pickup without continuous polling.

## 2.3.19

- Fixes a false **Bluetooth control service unavailable** GUI error after a successful device Connect action.
- Bluetooth status reads retain the short 4-second control-service health timeout.
- Pairing/device actions receive action-appropriate control-socket timeouts (30s pairing / 35s device actions); HTTP request inactivity allowance is 40s without changing core IPC timeouts.
- Successful Bluetooth device actions now queue the full inventory/media snapshot through the existing event-driven refresh worker instead of blocking the GUI request on a second expensive scan.
- Bluetooth pairing/control semantics, metadata/title/artist display, pairing-only rapid polling, dual-mono audio and conditional `sinc-fastest` resampling are otherwise unchanged from 2.3.18.

# mk-clock-adult 2.3.19 release notes

## Weather panel GUI clarity

- Weather panel controls are now mode-aware: irrelevant inputs are hidden instead of merely disabled.
- Inside sensor, Outside now, and Today low / high show no extra configuration fields.
- Hours ahead shows only the 1–48 hour offset input.
- Specific time shows only the forecast-hour input.
- Each panel shows a short explanation for the selected source.
- Weather data semantics and backend configuration are unchanged.

## System GUI cleanup

- Built directly from mk-clock-adult 2.3.16.
- Removed the following Storage rows from the System GUI because they are not useful on the production Banana Pi image and consistently report unavailable:
  - Boot partition
  - Boot filesystem
  - Boot mount
- Removed the corresponding browser-side diagnostics bindings.
- The lower-level diagnostics collector/API is retained for support/CLI use; only the GUI presentation is removed.
- Bluetooth pairing/control, pairing-only rapid polling, Bluetooth dual-mono audio path, conditional `sinc-fastest` resampling, local MP3/alarm playback, kernel, Device Tree and MAX98357A behavior are unchanged.

## Baseline

- Requires `bpi-zero-clock 1.0.3` / `bpi-m2-zero-r1`.
- Retains the lean 2.3.10+ APT dependency list.
