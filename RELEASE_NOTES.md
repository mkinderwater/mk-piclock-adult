# mk-clock-adult 2.1A BPI-M2 Zero R1 Release Notes

## Platform transition

The adult clock is now maintained only for the Banana Pi M2 Zero. Raspberry Pi adult development ends at 1.2.64.

Reference OS image: `Armbian_community_26.8.0-trunk.413_Bananapim2zero_trixie_current_6.18.38_minimal`.

- Documented the image-preparation workflow: Linux `e2fsprogs` prepares/validates ext4 compatibility and SharpExt4Explorer provides Windows read/write editing.
- Added `/root/.not_logged_in_yet` examples for both hidden and broadcast Wi-Fi SSIDs.
- Added `BOM.md` with the purchased hardware, supplier links, component roles, and selection rationale, including the selected external power cable and self-adhesive rubber feet, plus a short list of required build items not represented by the supplied purchase links.

## Ported hardware

- Board power: regulated +5 V to CON2 physical pin 4, GND to physical pin 6.
- SSD1322 SPI: `/dev/spidev0.0`.
- OLED D/C: PA2, line 2, physical pin 22.
- OLED reset: PA0, line 0, physical pin 13.
- Touch: PA21, line 21, physical pin 38.
- AHT10: TWI0 on physical pins 3 and 5, exposed as `/dev/i2c-0`.
- MAX98357A: PA18/PA19/PA20 I2S with PA1 codec shutdown.
- MAX98357A ALSA discovery and mono-to-stereo decoding are retained from the proven BPI kid-clock branch.

## Retained application functions

All adult 1.2.64 Weather, alarm, password, backup, restore, font, music, diagnostics, and OLED update behaviour is retained. The unused shared `$$` query helper was removed without changing GUI behaviour.

## Bug fix ported from mk-piclock kid clock

- `ipc_config_alarm()` now marks the OLED display dirty when an alarm is saved, matching the kid clock's v1.9.15 fix. Previously, changing an alarm's time or weekdays, or disabling the last enabled alarm, left the `ALARM` footer stale until an unrelated redraw or the next minute tick.

## Versions

```text
Product:     mk-clock-adult-2.1A-bpi-m2-zero-r1
HTTP API:    1.46
Private IPC: 27
Weather:     2.0.14
Platform:    BPI-M2 Zero
```
