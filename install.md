# mk-clock-adult 1.2.40 installation

This guide installs or upgrades mk-clock-adult on a Raspberry Pi or compatible Debian system using systemd. Run the commands from the extracted release directory.

## Requirements

* Raspberry Pi with the clock OLED, touch sensor, amplifier, and AHT10 hardware connected
* Debian 13 or another distribution providing libgpiod 2.x
* Network access for Weather and time synchronization
* A user with `sudo` access

Install the build dependencies:

```bash
sudo apt update
sudo apt install --no-install-recommends -y \
  build-essential \
  pkg-config \
  ca-certificates \
  tzdata \
  i2c-tools \
  libgpiod-dev \
  libfreetype-dev \
  libasound2-dev \
  libmpg123-dev \
  libmp3lame-dev \
  libmicrohttpd-dev \
  libcurl4-openssl-dev \
  libjson-c-dev
```

Confirm libgpiod 2.x is available:

```bash
pkg-config --modversion libgpiod
```

## Configure Raspberry Pi boot interfaces

This is the only authoritative `config.txt` configuration for this release.

The clock requires these settings:

| Setting                         | Purpose                                                                    |
| ------------------------------- | -------------------------------------------------------------------------- |
| `dtparam=spi=on`                | Enables SPI0 for the SSD1322 OLED.                                         |
| `dtparam=i2c_arm=on`            | Enables I2C1 on GPIO2 and GPIO3 for the AHT10.                             |
| `dtparam=audio=off`             | Disables the onboard audio path before loading the external I2S amplifier. |
| `dtoverlay=max98357a,no-sdmode` | Loads the MAX98357A I2S sound card without assigning an SD/EN GPIO.        |

The `dtparam=i2c_arm=on` setting enables the I2C controller. The separate `i2c-dev` kernel module creates the `/dev/i2c-1` device used by the application.

### 1. Locate and back up the active boot file

Current Raspberry Pi OS uses `/boot/firmware/config.txt`. Older images may use `/boot/config.txt`.

```bash
if [ -f /boot/firmware/config.txt ]; then
  BOOT_CONFIG=/boot/firmware/config.txt
elif [ -f /boot/config.txt ]; then
  BOOT_CONFIG=/boot/config.txt
else
  echo "Raspberry Pi config.txt was not found" >&2
  exit 1
fi

printf 'Using %s\n' "$BOOT_CONFIG"
sudo cp -a "$BOOT_CONFIG" \
  "$BOOT_CONFIG.backup-$(date +%Y%m%d-%H%M%S)"
```

### 2. Apply the complete `config.txt` block

Open the active file:

```bash
sudo nano "$BOOT_CONFIG"
```

Find the existing `[all]` section and add these lines beneath it.

If no `[all]` section exists, add the complete block at the end of the file:

```ini
[all]
dtparam=spi=on
dtparam=i2c_arm=on
dtparam=audio=off
dtoverlay=max98357a,no-sdmode
```

Do not also run:

```bash
sudo raspi-config nonint do_spi 0
sudo raspi-config nonint do_i2c 0
```

Those commands modify the same boot interface configuration and are unnecessary when `config.txt` is edited directly.

Keep each required line exactly once.

Remove or comment out conflicting entries elsewhere in the file, especially:

```ini
dtparam=spi=off
dtparam=i2c_arm=off
dtparam=audio=on
```

Remove duplicate or alternate MAX98357A overlays so only this line remains:

```ini
dtoverlay=max98357a,no-sdmode
```

Do not add:

```ini
dtparam=i2c_vc=on
```

That setting enables the reserved I2C0 bus, not I2C1 on GPIO2 and GPIO3.

Do not add a separate:

```ini
dtparam=i2s=on
```

The MAX98357A overlay configures the required I2S device-tree nodes.

Save the file, then inspect the relevant entries:

```bash
sudo grep -nE \
  '^[[:space:]]*(dtparam=(spi|i2c_arm|audio)=|dtoverlay=max98357a)' \
  "$BOOT_CONFIG"
```

The effective result must contain:

```text
dtparam=spi=on
dtparam=i2c_arm=on
dtparam=audio=off
dtoverlay=max98357a,no-sdmode
```

### 3. Enable the I2C device module

The `i2c-dev` kernel module must load during boot so `/dev/i2c-1` is created.

Create a dedicated modules-load configuration:

```bash
printf '%s\n' 'i2c-dev' |
  sudo tee /etc/modules-load.d/mk-piclock-i2c.conf >/dev/null
```

Load it immediately for the current session:

```bash
sudo modprobe i2c-dev
```

Confirm the persistent configuration:

```bash
cat /etc/modules-load.d/mk-piclock-i2c.conf
```

Expected output:

```text
i2c-dev
```

The immediate `modprobe` command may not create `/dev/i2c-1` until the I2C controller has been enabled through `config.txt` and the system has rebooted.

### 4. Reboot

A reboot is required after changing `config.txt`:

```bash
sudo reboot
```

### 5. Verify every boot interface

After reboot, confirm the required device nodes exist:

```bash
ls -l /dev/spidev0.0 /dev/i2c-1
```

Expected device types:

```text
/dev/spidev0.0
/dev/i2c-1
```

Confirm the I2C device module is loaded:

```bash
grep -w i2c_dev /proc/modules ||
  test -e /dev/i2c-1
```

Confirm Raspberry Pi boot interface state:

```bash
sudo raspi-config nonint get_spi
sudo raspi-config nonint get_i2c
```

Both commands must print:

```text
0
```

These commands only inspect the configured state. They do not change the configuration.

Confirm the I2S sound card is registered:

```bash
cat /proc/asound/cards
aplay -l
```

The output should include a MAX98357A playback device. Card numbering may vary.

Wire the AHT10 as documented in `pinouts.md`.

If upgrading an already running clock, stop the core before scanning the I2C bus:

```bash
sudo systemctl stop mk-piclock-core.service 2>/dev/null || true
sudo i2cdetect -y 1
```

Expected row excerpt:

```text
30: -- -- -- -- -- -- -- -- 38 -- -- -- -- -- -- --
```

Restart the core after the scan:

```bash
sudo systemctl start mk-piclock-core.service 2>/dev/null || true
```

The AHT10 uses address `0x38`.

The OLED uses SPI0 and does not conflict with the I2C bus.

The MAX98357A uses I2S and does not use `/dev/i2c-1`.

`make install` performs an I2C preflight before stopping or replacing services. On Raspberry Pi OS it checks the persistent I2C state. On all systems it requires `/dev/i2c-1`.

SPI and MAX98357A are verified using the commands above.

## Extract

```bash
unzip mk-clock-adult-1.2.40.zip
cd mk-clock-adult-1.2.40
```

## Build

```bash
make clean
make -j2
```

The build creates:

```text
mk-piclock-core
mk-piclock-api
weather/build/mk-piclock-weather
```

## Install or upgrade

```bash
make install
```

The installer:

* Stops the existing services
* Builds the release
* Preserves runtime settings and user music
* Installs the binaries and web GUI
* Enables and starts the required systemd units
* Requests an immediate Weather refresh

The following units are enabled:

```text
mk-piclock-core.service
mk-piclock-api.service
mk-piclock-weather.path
mk-piclock-weather.timer
```

Existing Weather source and panel settings under `/var/lib/mk-piclock-weather` are retained.

The protected built-in audio files are verified during installation.

Expected SHA-256 values:

```text
default-alarm.mp3  09c856ce9ef7b4bc9ea258f9b8c822e4ab4695642debfa2a4b3894d98c630fdc
message-chime.mp3  d4962210222af4a36c8cd5aba6998744ff7c1aa080509bf08400ca97cd31d855
```

## Verify

Check the services:

```bash
systemctl --no-pager --full status \
  mk-piclock-core.service \
  mk-piclock-api.service \
  mk-piclock-weather.path \
  mk-piclock-weather.timer
```

Confirm each required unit is active:

```bash
systemctl is-active mk-piclock-core.service
systemctl is-active mk-piclock-api.service
systemctl is-active mk-piclock-weather.path
systemctl is-active mk-piclock-weather.timer
```

Open the GUI at:

```text
http://<clock-ip>:8080/
```

The System page should show:

* Clock IP address
* Hostname
* Active network interface
* SSID and Wi-Fi signal when available
* NTP synchronization state

Check API discovery and product versions:

```bash
curl -s http://127.0.0.1:8080/api/v1 |
  python3 -m json.tool
```

Expected compatibility:

```text
Product:     mk-clock-adult-1.2.40
HTTP API:    1.37
Private IPC: 23
Weather:     Native C 2.0.10
```

## AHT10 ROOM sensor

The core polls the AHT10 every 10 seconds.

Check its live state:

```bash
curl -s http://127.0.0.1:8080/api/v1/status |
  python3 -c \
  'import json,sys; print(json.load(sys.stdin)["room_sensor"])'
```

A working sensor reports:

* `status: active`
* A decimal `temperature_c`
* A decimal `humidity_percent`
* The last `measured_at` timestamp

The OLED ROOM panel shows the fixed thermometer-and-droplet sprite, room temperature, and relative humidity.

Default systemd settings:

```ini
MK_AHT10_ENABLED=1
MK_AHT10_DEVICE=/dev/i2c-1
MK_AHT10_ADDRESS=0x38
MK_AHT10_POLL_SECONDS=10
MK_AHT10_STALE_SECONDS=120
```

To override them, create a systemd drop-in:

```bash
sudo systemctl edit mk-piclock-core.service
```

Example:

```ini
[Service]
Environment=MK_AHT10_POLL_SECONDS=15
Environment=MK_AHT10_STALE_SECONDS=180
```

Apply the changes:

```bash
sudo systemctl daemon-reload
sudo systemctl restart mk-piclock-core.service
```

Sensor states:

* `active`: Current reading and normal sensor sprite
* `stale`: Last reading retained temporarily with the clock-badged stale sprite
* `error`: No usable reading; `--° --%` and sensor-with-X sprite
* `disabled`: Sensor disabled by configuration

## Weather

Run an immediate refresh:

```bash
sudo systemctl start mk-piclock-weather.service
```

Inspect Weather status and logs:

```bash
systemctl --no-pager --full status mk-piclock-weather.service
journalctl -u mk-piclock-weather.service -n 100 --no-pager
```

The Weather source is stored at:

```text
/var/lib/mk-piclock-weather/weather-source.url
```

The scheduled refresh runs about every 15 minutes. It also runs when the Weather source or panel configuration changes.

Ended ECCC alerts are discarded immediately after a successful refresh.

## Logs and troubleshooting

### Core logs

```bash
journalctl -u mk-piclock-core.service -n 100 --no-pager
```

### AHT10 logs

```bash
journalctl -u mk-piclock-core.service -n 200 --no-pager |
  grep room-sensor
```

### Missing `/dev/i2c-1`

Confirm the active boot file contains:

```ini
dtparam=i2c_arm=on
```

Confirm the persistent module configuration exists:

```bash
cat /etc/modules-load.d/mk-piclock-i2c.conf
```

Expected output:

```text
i2c-dev
```

Try loading the module manually:

```bash
sudo modprobe i2c-dev
```

Inspect the kernel device-tree state:

```bash
test -d /proc/device-tree/soc &&
  grep -RIl 'i2c' /proc/device-tree/aliases 2>/dev/null
```

Check for I2C-related boot errors:

```bash
journalctl -b -k --no-pager |
  grep -Ei 'i2c|device tree|overlay'
```

List available I2C buses:

```bash
i2cdetect -l
```

If no buses are listed after confirming the boot file and module configuration, reboot and verify that the correct `config.txt` file was edited.

### I2C permission errors

Verify the device exists:

```bash
ls -l /dev/i2c-1
```

Verify the service account belongs to the `i2c` group:

```bash
id mk-piclock-core
```

Verify the installed udev rule assigned the device to the `i2c` group.

### API and GUI logs

```bash
journalctl -u mk-piclock-api.service -n 100 --no-pager
```

### Follow all clock services

```bash
journalctl -f \
  -u mk-piclock-core.service \
  -u mk-piclock-api.service \
  -u mk-piclock-weather.service
```

### Restart the clock stack

```bash
sudo systemctl restart \
  mk-piclock-core.service \
  mk-piclock-api.service

sudo systemctl start mk-piclock-weather.service
```

If the GUI still shows old assets after an upgrade, perform a hard browser refresh.

Release 1.2.40 uses a new asset version, so normal browser reloads should fetch the updated files.

## Touch network diagnostics

With no audio or alarm active, hold the TTP223B sensor continuously for 8 seconds.

The OLED displays:

* SSID
* Signal percentage and dBm
* IPv4 address
* Hostname

The screen refreshes every 2 seconds and closes after 30 seconds or on the next touch.

Releasing after 3 seconds but before 8 seconds retains the random-music action.

## Clean build files

```bash
make clean
```

## Uninstall

```bash
make uninstall
```

Uninstall removes:

* Installed binaries
* Service files
* The installed web GUI
* API specification
* Protected built-in audio files

User-created music and persistent clock configuration under `/opt/mk-piclock` are not broadly deleted by this target.
