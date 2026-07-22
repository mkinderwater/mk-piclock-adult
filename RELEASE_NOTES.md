# mk-clock-adult 1.2.40 Release Notes

Release 1.2.40 consolidates Raspberry Pi boot configuration documentation without changing runtime behavior.

## Installation documentation

- Made `install.md` the single authoritative source for every required `config.txt` modification.
- Consolidated SPI0, I2C1, onboard-audio disablement, and the MAX98357A overlay into one exact `[all]` block.
- Added active boot-file detection for `/boot/firmware/config.txt` and legacy `/boot/config.txt`.
- Added a timestamped backup command before editing the boot file.
- Added conflict cleanup for disabled SPI/I2C, enabled onboard audio, and duplicate MAX98357A overlays.
- Clarified that `dtparam=i2c_vc=on` and a separate `dtparam=i2s=on` are not required by this project.
- Added one reboot sequence and post-boot checks for `/dev/spidev0.0`, `/dev/i2c-1`, the AHT10 address, and the ALSA sound card.
- Removed repeated boot-setting snippets from `pinouts.md` to prevent documentation drift.
- Added a regression test that requires the complete authoritative block in `install.md` and rejects copied boot settings in `pinouts.md`.

## Interfaces

- Product: mk-clock-adult-1.2.40
- HTTP API: 1.37
- Private IPC: 23
- Native Weather: 2.0.10
- Room sensor: Native AHT10

No native behavior, API schema, panel logic, weather logic, AHT10 logic, or runtime assets changed from 1.2.39.
