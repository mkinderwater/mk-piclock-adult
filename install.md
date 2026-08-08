# mk-clock-adult 2.1A BPI-M2 Zero R1 installation

This release supports the Banana Pi M2 Zero running Armbian. It does not install on Raspberry Pi OS and does not edit boot configuration automatically.

## Power wiring

Feed the board directly from a regulated 5 V supply through CON2:

- +5 V to physical pin 4.
- GND to physical pin 6.

Pin 6 is common ground and is also used by the OLED wiring in `pinouts.md`. Do not connect a second board-power source at the same time.

## Packages

```bash
sudo apt update
sudo apt install -y \
  build-essential pkg-config ca-certificates tzdata \
  device-tree-compiler i2c-tools alsa-utils unzip \
  libgpiod-dev libfreetype6-dev libasound2-dev libmpg123-dev \
  libmicrohttpd-dev libmp3lame-dev libcurl4-openssl-dev libjson-c-dev
```

## Enable BPI interfaces

Edit `/boot/armbianEnv.txt`. Preserve existing values and ensure the active entries include:

```text
overlays=spi-spidev i2c0
param_spidev_spi_bus=0
user_overlays=max98357a-bpi-m2-zero
```

There must be only one `overlays=` line and one `user_overlays=` line. Add other required overlays to those same lines rather than creating duplicates.

`spi-spidev` enables the generic `spidev` device, and `param_spidev_spi_bus=0` selects SPI bus 0. A separate `spi0` overlay is not required for Armbian's H3 overlay implementation.

## Install the MAX98357A overlay

Compile and register the bundled overlay:

```bash
sudo armbian-add-overlay hardware/max98357a-bpi-m2-zero.dts
```

Confirm `/boot/armbianEnv.txt` includes `max98357a-bpi-m2-zero` in `user_overlays=`. The bundled source uses `simple-audio-card,mclk-fs = <256>`, PA18/PA19/PA20 for I2S, and PA1 for amplifier SD/EN.

Reboot after enabling the overlays:

```bash
sudo reboot
```

## Verify hardware

```bash
ls -l /dev/spidev0.0 /dev/gpiochip0 /dev/i2c-0
sudo i2cdetect -y 0
cat /proc/asound/cards
aplay -l
```

The AHT10 should appear at address `38`. The sound card should contain `MAX98357A`.

## Build and install

```bash
unzip mk-clock-adult-2.1A-bpi-m2-zero-r1-release.zip
cd mk-clock-adult-2.1A-bpi-m2-zero-r1
make test
sudo make install
```

`make install` preserves `/opt/mk-piclock/config`, uploaded music, and fonts. It rebuilds the Native Weather service for the BPI CPU. It does not change `/boot/armbianEnv.txt` or install the Device Tree overlay.

## Verify services

```bash
systemctl --no-pager --full status \
  mk-piclock-core.service \
  mk-piclock-api.service \
  mk-piclock-weather.timer
journalctl -u mk-piclock-core.service -n 100 --no-pager
curl http://127.0.0.1:8080/api/v1/health
```

Expected identity:

```text
Product:     mk-clock-adult-2.1A-bpi-m2-zero-r1
HTTP API:    1.46
Private IPC: 27
Weather:     2.0.14
Platform:    BPI-M2 Zero
```

## Audio override

The core searches ALSA cards for `MAX98357A` and opens the first playback device with `plughw`. To force a device, add an override to `mk-piclock-core.service`:

```ini
Environment=MK_PICLOCK_ALSA_DEVICE=plughw:0,0
```

Then run:

```bash
sudo systemctl daemon-reload
sudo systemctl restart mk-piclock-core.service
```

## AHT10 override

The default is `/dev/i2c-0`, address `0x38`. The service supports:

```ini
Environment=MK_AHT10_DEVICE=/dev/i2c-0
Environment=MK_AHT10_ADDRESS=0x38
Environment=MK_AHT10_POLL_SECONDS=10
Environment=MK_AHT10_STALE_SECONDS=120
```

## Reset a lost web password

```bash
sudo rm -f /opt/mk-piclock/config/web-password.txt \
  /opt/mk-piclock/config/.web-password.tmp
sudo systemctl restart mk-piclock-api.service
```

## Upgrade policy

Adult releases after 1.2.64 target only the BPI-M2 Zero. Do not merge future adult changes back into the retired Raspberry Pi branch.
