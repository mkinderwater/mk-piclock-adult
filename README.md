# mk-clock-adult 2.1A BPI-M2 Zero R1

Native Banana Pi M2 Zero alarm clock software for the SSD1322 OLED, TTP223B touch input, MAX98357A I2S audio, ECCC Weather, and an AHT10 inside temperature and humidity sensor.

This is now the maintained adult-clock platform. The Raspberry Pi adult branch ends at 1.2.64 and receives no further updates.

Validated OS image: `Armbian_community_26.8.0-trunk.413_Bananapim2zero_trixie_current_6.18.38_minimal`.

For pre-boot Windows customization, the ext4 filesystem is prepared with Linux `e2fsprogs` (`tune2fs`/`e2fsck`) and then edited with SharpExt4Explorer. `install.md` includes hidden and broadcast SSID examples for `/root/.not_logged_in_yet`.

## BPI port

- Board power is fed directly through CON2: +5 V on physical pin 4 and GND on physical pin 6.
- Allwinner GPIO mapping through `/dev/gpiochip0`.
- SSD1322 on `/dev/spidev0.0`.
- AHT10 on `/dev/i2c-0` using header pins 3 and 5.
- MAX98357A card discovery with optional `MK_PICLOCK_ALSA_DEVICE` override.
- Forced stereo MP3 decoding for correct H2+/H3 I2S framing.
- Custom MAX98357A Device Tree overlay with 256x master clock and codec-controlled PA1 shutdown.
- No MAX98357A startup/stop click observed on this platform; see `pinouts.md` for why.
- BPI-only installer, permissions, wiring, diagnostics, and documentation.
- Removed the unused GUI `$$` helper.

All 1.2.64 Weather, alarm, web password, backup, font, OLED partial-update, and AHT10 functions are retained.

## Bill of materials

See `BOM.md` for the reference hardware purchase list, exact supplier links, component roles, and the design logic behind each selected part.

## Versions

```text
Product:     mk-clock-adult-2.1A-bpi-m2-zero-r1
HTTP API:    1.46
Private IPC: 27
Weather:     Native C 2.0.14
Platform:    BPI-M2 Zero
```

See `install.md` and `pinouts.md`.
