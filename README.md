# mk-clock-adult 2.3.54

Adult bedside clock for Banana Pi M2 Zero using the `bpi-zero-clock 1.0.4-preview36` playback hardware baseline and kernel `6.12.101+deb13-armmp`.

This release is playback-only. Microphone, voice capture, speech DSP and assistant functions are not included.

## What it does

- SSD1322 256x64 OLED clock.
- 12/24-hour time.
- Alarms and bedtime dimming.
- MAX98357A audio for alarms, music and podcasts.
- AHT10 indoor temperature and RH.
- Indoor temperature trend arrow.
- Weather panels with last-good cache recovery.
- Uploaded TTF/OTF clock fonts.
- Browser GUI for Dashboard, Alarms, Music, Podcasts, Weather, Display, System and Recent Activity.
- Persistent settings and media across normal upgrades.

## Podcasts

Podcasts use a separate audio library. They are never included in Music or alarm selection.

During podcast playback the OLED stays in clock mode. The podcast title replaces the date line.

Podcast volume is independent from Music and alarm volume.

### Random podcast playback

Ten short touch taps within eight seconds starts one random unplayed podcast when the shortcut is enabled.

The rules are simple:

- One request plays one MP3.
- Playback stops when that MP3 ends.
- A podcast is marked played when manual or random playback starts.
- Random playback excludes played podcasts.
- Play history survives reboot and upgrade.
- After all podcasts have played, the next random request clears history and starts a new cycle.
- **Reset play history** starts a new cycle immediately.

History file:

```text
/opt/mk-piclock/config/podcast-history.txt
```

The GUI uses a lightweight summary endpoint for `Unplayed X of Y`. Large libraries do not wait for full MP3 metadata parsing before showing the count.

### Browser uploads

The Podcasts page accepts up to 14 MP3 files per browser upload.

Uploads are staged. Processing does not start automatically.

### SSH/SFTP bulk uploads

For large libraries, upload MP3 files to:

```text
/opt/mk-piclock/assets/podcasts/upload/
```

Finish the transfer first. Then:

1. Open **Podcasts**.
2. Select **Scan uploads**.
3. Select **Process N podcasts**.

The upload directory is not watched in the background.

Processing is one file at a time. This keeps CPU, storage I/O and the GUI responsive during large imports.

Processing profile:

```text
MP3
96 kbps
mono
44.1 kHz
16 kHz low-pass
```

Each file follows this path:

```text
validate source
process audio
validate output
move output into active library
delete successful source upload
```

Failed source files remain in `upload/`.

At completion the GUI shows processed, failed and total counts. Only failed files and their reasons are listed.

Active podcast library:

```text
/opt/mk-piclock/assets/podcasts/
```

The library scan supports up to 1024 entries.

## Indoor temperature trend

The indoor panel draws the trend arrow directly into the OLED framebuffer. It does not depend on the selected TTF font.

Trend rules:

- Sample once per minute.
- Compare two smoothed 3-minute averages about 10 minutes apart.
- Show `UP` when temperature rises by at least `0.15 C`.
- Show `DOWN` when temperature falls by at least `0.15 C`.
- Keep an active arrow until movement returns inside about `+/-0.08 C`.
- Show no arrow when stable.

When an arrow is active, temperature moves up and the arrow uses its own line. When no arrow is required, temperature returns to its original centered position.

RH remains on the lower line.

Trend history is held in RAM. A core restart requires enough new sensor history before an arrow can appear again.

## Weather

Weather uses a last-good cache so a reboot or temporary Internet loss does not immediately blank valid panels.

The weather service republishes cached data when the core restarts and only commits runtime/cache state after the core accepts the update.

## Music

- Browser upload limit: 14 files per batch.
- Transcoding runs one file at a time.
- New uploads are rejected while Music transcoding is active.

## Fonts

Uploaded TTF/OTF fonts are added to the font list. Uploading a font does not automatically select it.

Replacing the currently selected uploaded font refreshes the cache while keeping that font selected.

## Upgrade persistence

Normal upgrades preserve:

```text
/opt/mk-piclock/config/
/opt/mk-piclock/assets/music/
/opt/mk-piclock/assets/podcasts/
/opt/mk-piclock/assets/podcasts/upload/
/opt/mk-piclock/assets/fonts/
```

Weather source, configuration and cached weather state are also preserved.

This includes alarms, display settings, audio settings, web settings and podcast play history.

## Hardware baseline

MAX98357A:

```text
BCLK  PA19  physical pin 27
LRC   PA18  physical pin 28
DIN   PA20  physical pin 40
SD/EN PA1   physical pin 11
```

TTP223B:

```text
VCC  physical pin 17
GND  physical pin 39
OUT  PA17 / physical pin 37
```

PA21 / physical pin 38 is free.

OLED: SSD1322 256x64 over SPI0.0.

Indoor sensor: AHT10 on `/dev/i2c-0`, address `0x38`.

See `pinouts.md` for complete wiring.

## Versions

```text
Product: mk-clock-adult-2.3.54-bpi-m2-zero-r1
HTTP API: v1.62
Private core/API IPC protocol: v35
```

## Manuals

Read `clock-instructions.md` for day-to-day use, touch controls, alarms, Music, Podcasts, Weather, Display, System, backups, and upgrades.

Read `assembly-instructions.md` to build the physical clock, wire each module, position the sensor/touch/speaker, perform the pre-power inspection, and verify the finished hardware.

## Install

Read `INSTALL.md`, then run:

```bash
./install.sh
```
