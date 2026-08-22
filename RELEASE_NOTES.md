# mk-clock-adult 2.3.54 release notes

## Release baseline

```text
Hardware: Banana Pi M2 Zero
Base image: bpi-zero-clock 1.0.4-preview36
Kernel: 6.12.101+deb13-armmp
HTTP API: v1.62
IPC: v35
```

## Podcasts

Added a dedicated Podcast mode for bedtime/background listening.

- Clock view remains active during playback.
- Podcast title replaces the date line.
- Podcast volume is independent.
- Ten short taps can start one random podcast.
- One request plays one MP3, then stops.
- Random selection excludes played podcasts.
- Play history survives reboot and upgrade.
- After the full library is played, the next random request clears history and starts a new cycle.
- GUI includes **Reset play history**.
- `Unplayed X of Y` uses a lightweight summary endpoint.
- Podcast library supports up to 1024 entries.

### Podcast import

Added staged browser and SSH/SFTP imports.

Bulk upload path:

```text
/opt/mk-piclock/assets/podcasts/upload/
```

Workflow:

```text
upload files
Scan uploads
Process N podcasts
```

Processing is one file at a time using:

```text
MP3 / 96 kbps / mono / 44.1 kHz / 16 kHz low-pass
```

Successful source files are deleted only after output validation and promotion into the active library.

Failed source files remain in the upload directory. The GUI shows processed, failed and total counts and lists only failures.

## Indoor temperature trend

Added a framebuffer-drawn indoor temperature trend arrow.

- Sample once per minute.
- Compare smoothed 3-minute averages about 10 minutes apart.
- Rising threshold: `+0.15 C`.
- Falling threshold: `-0.15 C`.
- Clear hysteresis: about `+/-0.08 C`.
- Arrow uses its own OLED line.
- Stable temperature shows no arrow.
- With no arrow, temperature returns to its original centered position.
- RH remains on the lower line.

## Upgrade persistence

Normal upgrades now explicitly preserve `/opt/mk-piclock/config/` and user media.

Preserved state includes:

- alarms
- clock settings
- display/audio settings
- web settings
- podcast play history
- Music library
- Podcast library
- staged Podcast uploads
- uploaded fonts
- weather configuration/cache

Transactional rollback also includes configuration state.

## GUI

- Music and Podcasts use the same cards, controls and progress treatment.
- Podcast batch processing uses one progress card regardless of library size.
- Podcast details stay open during refresh.
- Per-item Played/Unplayed badges were removed.
- Dashboard correctly identifies Podcast playback.
- Music no longer reports Podcast playback as Music.
- GUI cache identifiers match the release version.

## Weather

- Last-good weather can be restored after reboot without requiring Internet first.
- Cached weather is republished after a core restart when required.
- Cache/runtime state is updated only after the core accepts the weather update.
- Icon state rolls back if publication fails.

## Packaging

Fixed executable permissions in the release ZIP.

Required scripts ship as `0755`, including:

```text
install.sh
hardware/verify-bpi-hardware.sh
packaging/build-release.sh
scripts/deploy.sh
scripts/verify-install.sh
weather/install.sh
weather/uninstall.sh
```

Release validation now fails if required scripts are not executable.

## Hardware

Current touch wiring:

```text
VCC -> physical pin 17
GND -> physical pin 39
OUT -> PA17 / physical pin 37
```

PA21 / physical pin 38 is free.

See `HARDWARE_MIGRATION.md` if upgrading an older clock wired for pin 38.
