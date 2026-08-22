# mk-clock-adult 2.3.54 install

## Requirements

Use:

- Banana Pi M2 Zero.
- `bpi-zero-clock 1.0.4-preview36`.
- Kernel `6.12.101+deb13-armmp`.
- Root shell.
- Correct system date/time.
- Wiring from `pinouts.md`.

If touch is still wired to physical pin 38, read `HARDWARE_MIGRATION.md` first.

## Install or upgrade

Extract the ZIP, enter the release directory and run:

```bash
./install.sh
```

Release scripts ship executable. No chmod step should be required.

If file permissions were lost while copying/extracting the source tree, restore only the executable bit:

```bash
chmod 755 install.sh hardware/verify-bpi-hardware.sh \
  packaging/build-release.sh scripts/deploy.sh scripts/verify-install.sh \
  weather/install.sh weather/uninstall.sh
```

Then run:

```bash
./install.sh
```

## Installer checks

The installer verifies:

- System clock is sane.
- Base image is `bpi-zero-clock 1.0.4-preview36`.
- Kernel matches the release baseline.
- MAX98357A playback hardware is ready.
- Touch is PA17 / physical pin 37.
- Required Debian packages are installed.
- Product identity is correct.
- GUI/API contract is v1.62.
- Required release scripts are executable.
- Default audio assets are valid.
- Weather payload is valid.

The installer then builds and deploys the core, API and weather services transactionally.

## Upgrade persistence

A normal upgrade keeps user data.

Preserved paths:

```text
/opt/mk-piclock/config/
/opt/mk-piclock/assets/music/
/opt/mk-piclock/assets/podcasts/
/opt/mk-piclock/assets/podcasts/upload/
/opt/mk-piclock/assets/fonts/
```

Weather source, weather configuration and cached weather state are also kept.

This preserves:

- alarms
- display settings
- audio settings
- web settings
- podcast play history
- music
- podcasts
- staged podcast uploads
- uploaded fonts

If deployment fails, the installer restores the previous release and snapshotted configuration.

## Bulk podcast import

Upload large podcast libraries by SSH/SFTP to:

```text
/opt/mk-piclock/assets/podcasts/upload/
```

After the transfer completes:

1. Open **Podcasts**.
2. Select **Scan uploads**.
3. Select **Process N podcasts**.

The directory is not processed automatically.

A successful import removes the source upload only after the processed MP3 validates and is moved into the active library.

Failed source files remain in `upload/`.

## Verify after install

Check services:

```bash
systemctl --no-pager --full status mk-piclock-core mk-piclock-api mk-piclock-weather
```

Check hardware metadata:

```bash
grep -E '^(VERSION|KERNEL_ABI|AUDIO_MODE|AUDIO_CAPTURE|I2S_RX_GPIO|I2S_RX_HEADER_PIN|TOUCH_GPIO|TOUCH_HEADER_PIN)=' \
  /etc/bpi-zero-clock-release
```

Expected touch values:

```text
I2S_RX_GPIO=unassigned
I2S_RX_HEADER_PIN=38-free
TOUCH_GPIO=PA17
TOUCH_HEADER_PIN=37
```

Open **System** in the GUI and confirm:

```text
mk-clock-adult 2.3.54
HTTP API v1.62
IPC v35
```

Installer log:

```text
/var/log/mk-clock-adult-install.log
```
