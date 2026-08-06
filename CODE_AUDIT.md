# mk-clock-adult 1.2.65 BPI-M2 Zero R1 Code Audit

## Scope

The 1.2.64 adult application was ported to the proven BPI-M2 Zero hardware path without removing adult Weather or AHT10 functions.

## Verified changes

- Hardware values are centralized in `hardware_profile.h`.
- Raspberry Pi BCM GPIO values are no longer compiled.
- AHT10 defaults to `/dev/i2c-0`.
- ALSA searches for the MAX98357A card and supports an explicit override.
- MP3 decoding forces stereo for H2+/H3 I2S framing.
- BPI udev and system-user files include SPI, GPIO, I2C, and audio access.
- The installer does not edit boot files or overlays.
- The unused GUI `$$` helper is removed.
- Product and web cache keys are `mk-clock-adult-1.2.65-bpi-m2-zero-r1`.

The test suite covers release identity, BPI documentation, AHT10 decoding, alarm safety, OLED partial updates, Weather rendering, diagnostics, backup, password protection, and GUI modules.
