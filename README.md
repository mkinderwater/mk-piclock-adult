# mk-clock-adult 1.2.40

Native Raspberry Pi alarm clock software for the SSD1322 OLED, TTP223B touch input, MAX98357A I2S audio, ECCC weather, and an AHT10 room temperature and humidity sensor.

## What changed in 1.2.40

- Consolidated every required Raspberry Pi `config.txt` modification into one authoritative section in `install.md`.
- Documented SPI0, I2C1, onboard-audio disablement, and the MAX98357A overlay together.
- Added active boot-file detection, backup instructions, conflict cleanup, reboot requirements, and post-boot verification.
- Removed repeated boot-setting snippets from `pinouts.md` so configuration rules cannot drift between documents.
- Preserved all 1.2.39 code, API, IPC, Weather, AHT10, panel, and asset behavior.

## Compatibility

```text
Product:     mk-clock-adult-1.2.40
HTTP API:    1.37
Private IPC: 23
Weather:     Native C 2.0.10
Room sensor: Native AHT10
```

## Build and install

See `install.md` for dependencies, the complete boot configuration, AHT10 wiring, build commands, installation, verification, and troubleshooting.

## Hardware

See `pinouts.md` for the complete production header map. The AHT10 defaults to `/dev/i2c-1` at address `0x38` using GPIO2/SDA1 and GPIO3/SCL1.
