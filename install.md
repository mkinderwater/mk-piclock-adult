# mk-clock-adult 2.1A BPI-M2 Zero R1 installation

This release supports the Banana Pi M2 Zero running Armbian. It does not install on Raspberry Pi OS and does not edit boot configuration automatically.

Before assembling hardware, see `BOM.md` for the reference parts, supplier links, and purchase rationale.

## Reference Armbian image

This release is developed and validated against:

```text
Armbian_community_26.8.0-trunk.413_Bananapim2zero_trixie_current_6.18.38_minimal
```

Use this image as the reference OS for a production clock. Other Armbian builds may work, but kernel, Device Tree, overlay, SPI, I2C, GPIO, or ALSA behaviour can differ.

## Preparing the Armbian image for Windows editing

The reference Armbian image uses an ext4 root filesystem. Windows does not natively provide normal File Explorer read/write access to ext4, so the image is prepared with Linux filesystem tools and then edited on Windows with **SharpExt4Explorer**.

The software roles are:

- **e2fsprogs** on Linux: provides `tune2fs` and `e2fsck`, which are used offline to inspect/adjust ext4 filesystem feature flags and validate the filesystem. This is the software used to make the ext4 filesystem compatible with the Windows-side editor when the stock image contains an ext4 feature the editor cannot handle.
- **SharpExt4Explorer** on Windows: opens the raw Armbian disk image or a flashed SD card and provides read/write access to the ext4 partition. It is the editor, not the tool used to alter ext4 filesystem features.

The compatibility step does **not** convert ext4 to another filesystem. The image remains ext4 and remains bootable by Armbian. Always work on an unmounted copy of the image and run `e2fsck` after changing filesystem feature flags.

To inspect the ext4 features from Linux, attach the raw image to a loop device and list its partitions, then inspect the root partition. Example:

```bash
sudo losetup --find --show --partscan Armbian_community_26.8.0-trunk.413_Bananapim2zero_trixie_current_6.18.38_minimal.img
lsblk
sudo tune2fs -l /dev/loop0p1 | grep 'Filesystem features'
```

Use the loop device actually returned by `losetup`; it may not be `/dev/loop0`. Any feature change must be made while the ext4 filesystem is unmounted, followed by a forced filesystem check:

```bash
sudo e2fsck -f /dev/loop0p1
sudo losetup -d /dev/loop0
```

After compatibility preparation, open the `.img` file in SharpExt4Explorer and edit files in the root ext4 partition directly.

> **Credential warning:** first-boot Wi-Fi settings are stored as plaintext in `/root/.not_logged_in_yet`. Do not distribute a customized image containing production credentials.

## Preconfigure first-boot Wi-Fi

Before first boot, SharpExt4Explorer can be used to create or replace:

```text
/root/.not_logged_in_yet
```

The file is a shell configuration sourced by Armbian's first-login process. The settings below are for the validated reference image.

### Hidden SSID example

For a hidden SSID, override Armbian's `createYAML()` function so Netplan receives `hidden: true`:

```bash
# /root/.not_logged_in_yet

# Network
PRESET_NET_CHANGE_DEFAULTS="1"
PRESET_NET_ETHERNET_ENABLED="0"
PRESET_NET_WIFI_ENABLED="1"
PRESET_NET_WIFI_SSID="MJ - IoT"
PRESET_NET_WIFI_KEY="8887282823"
PRESET_NET_WIFI_COUNTRYCODE="CA"
PRESET_CONNECT_WIRELESS="n"

# DHCP
PRESET_NET_USE_STATIC="0"

# Locale and timezone
SET_LANG_BASED_ON_LOCATION="n"
PRESET_LOCALE="en_CA.UTF-8"
PRESET_TIMEZONE="America/Edmonton"

# Override Armbian's Netplan generator to support a hidden SSID.
createYAML() {
cat <<EOF
network:
  version: 2
  wifis:
    ${DEVICE_NAME}:
      dhcp4: true
      dhcp6: true
      regulatory-domain: CA
      access-points:
        "MJ - IoT":
          password: "8887282823"
          hidden: true
EOF
}
```

### Normal/broadcast SSID example

For a normal SSID that broadcasts its name, **do not override `createYAML()`**. Armbian's built-in first-login generator handles the standard Wi-Fi configuration:

```bash
# /root/.not_logged_in_yet

# Network
PRESET_NET_CHANGE_DEFAULTS="1"
PRESET_NET_ETHERNET_ENABLED="0"
PRESET_NET_WIFI_ENABLED="1"
PRESET_NET_WIFI_SSID="MJ - IoT"
PRESET_NET_WIFI_KEY="8887282823"
PRESET_NET_WIFI_COUNTRYCODE="CA"
PRESET_CONNECT_WIRELESS="n"

# DHCP
PRESET_NET_USE_STATIC="0"

# Locale and timezone
SET_LANG_BASED_ON_LOCATION="n"
PRESET_LOCALE="en_CA.UTF-8"
PRESET_TIMEZONE="America/Edmonton"
```

For another installation, replace the SSID and key in both places in the hidden-SSID example, or in the preset variables in the normal-SSID example. Keep the country code `CA` for Canadian deployments.

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
