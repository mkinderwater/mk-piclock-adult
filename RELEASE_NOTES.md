# mk-clock-adult 1.2.65 BPI-M2 Zero R1 Release Notes

## Platform transition

The adult clock is now maintained only for the Banana Pi M2 Zero. Raspberry Pi adult development ends at 1.2.64.

## Ported hardware

- SSD1322 SPI: `/dev/spidev0.0`.
- OLED D/C: PA2, line 2, physical pin 22.
- OLED reset: PA0, line 0, physical pin 13.
- Touch: PA21, line 21, physical pin 38.
- AHT10: TWI0 on physical pins 3 and 5, exposed as `/dev/i2c-0`.
- MAX98357A: PA18/PA19/PA20 I2S with PA1 codec shutdown.
- MAX98357A ALSA discovery and mono-to-stereo decoding are retained from the proven BPI kid-clock branch.

## Retained application functions

All adult 1.2.64 Weather, alarm, password, backup, restore, font, music, diagnostics, and OLED update behaviour is retained. The unused shared `$$` query helper was removed without changing GUI behaviour.

## Versions

```text
Product:     mk-clock-adult-1.2.65-bpi-m2-zero-r1
HTTP API:    1.46
Private IPC: 27
Weather:     2.0.14
Platform:    BPI-M2 Zero
```
