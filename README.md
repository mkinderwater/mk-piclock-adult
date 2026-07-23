# mk-clock-adult 1.2.62

mk-clock-adult is a purpose-built bedside clock for Raspberry Pi. It combines a clear OLED clock, dependable alarms, local indoor climate readings, official Environment and Climate Change Canada weather, and simple music playback in one small appliance.

It is not trying to replace a phone, smart speaker, or full home-automation system. The goal is more modest: show useful information at a glance, wake up reliably, remain easy to operate in the dark, and provide enough diagnostics that the person maintaining it can understand what is happening.

## At a glance

- Large 12-hour or 24-hour clock on a 256 x 64 SSD1322 OLED
- Three independently configurable Weather panels
- AHT10 indoor temperature and humidity sensing
- Current and forecast ECCC GeoMet weather
- Daily low, high, and precipitation summary
- Rotating ECCC warning marquee with an optional chime
- Seven repeating alarms with gradual volume increase
- Uploaded MP3 music, metadata, playback, and alarm selection
- Touch control for stopping audio, playing music, and viewing network details
- Automatic nighttime dimming
- Browser-based controls with an exact live OLED preview
- Detailed Raspberry Pi, network, storage, SD-card, sensor, and service diagnostics
- Downloadable configuration backup and staged restore
- Local HTTP API with an included OpenAPI specification
- Native C services managed and hardened by systemd

## Designed as a clock first

The main screen uses a 40/20/20/20 layout:

```text
Clock, location and date | Panel 1 | Panel 2 | Panel 3
```

The left side remains dedicated to the time. The three smaller panels can each show the information most useful to the owner.

### Clock display

- Large clock with a blinking colon
- 12-hour and 24-hour formats
- Date beneath the time
- Configured Weather location above the time
- `ALARM ON` indicator when at least one alarm is enabled for one or more weekdays
- Thin seconds-position line with a moving dark pixel
- Automatic centring based on the pixels actually rendered, not only font metrics
- Built-in pixel font fallback when a selected TrueType or OpenType font cannot be used
- Screen-off and return-to-clock controls

The pixel-based centring allows different fonts and digit combinations to remain visually balanced, including narrow digits such as `1`.

### Fonts

- Automatic font detection
- Installed Linux fonts
- Uploaded `.ttf` and `.otf` fonts
- Built-in OLED font
- Browser font preview
- Adjustable clock font size from 18 to 54
- Separate font choice for the large INSIDE temperature
- Safe fallback when an uploaded or system font is removed

Automatic selection keeps a valid saved choice. When no valid choice exists, it prefers an uploaded font, then DejaVu Sans Mono, then the built-in renderer.

### Bedtime display

- Optional daily bedtime schedule
- Configurable start and end times
- Night brightness from 0 to 100 percent
- Live brightness preview on the physical OLED while moving the browser slider
- Full brightness outside the configured bedtime window

The physical OLED colour is fixed by the panel. The GUI colour setting, Yellow, Green, or White, makes the browser preview match the installed display.

## Indoor climate

The INSIDE panel reads an AHT10 sensor directly over I2C. It does not depend on the internet or the Weather service.

- Temperature shown with one decimal place
- Whole-percent relative humidity
- Independent font selection for the temperature
- Ten-second default polling interval
- Stale-reading detection
- Clear unavailable state using `--.-°` and `--%`
- Sensor state and last-reading time in the GUI and diagnostics

Sensor states are handled deliberately:

- **Active:** current values at normal brightness
- **Stale:** the last valid values remain visible but are dimmed
- **Error:** no usable reading is shown
- **Disabled:** the sensor has been disabled by configuration

Accurate readings depend on placement. The AHT10 should be kept away from the Raspberry Pi CPU, OLED regulator, amplifier, speaker, direct sunlight, and trapped enclosure heat.

## Weather

Weather is supplied by the official ECCC GeoMet city feed and processed by a separate native C service.

### Three independent panels

Each panel may be configured as:

- **INSIDE:** local AHT10 temperature and humidity
- **OUTSIDE:** current ECCC conditions
- **TODAY:** current-day low, high, and maximum precipitation probability
- **Hours ahead:** an hourly forecast from 1 through 48 hours ahead
- **Specific time:** the next occurrence of a selected local forecast hour

Panels may repeat a source. For example, a layout may use INSIDE, OUTSIDE, and TODAY, or three different forecast times.

### TODAY panel

TODAY uses a compact three-row layout:

```text
TODAY
L    18°
H    32°
POP  20%
```

The labels share one left edge and the values share one right edge. The Weather service evaluates available hourly values on the current local date, includes the current observation when useful, and reports the highest available hourly precipitation probability for the day.

The API retains low and high occurrence-hour data even though the OLED intentionally omits those times for clarity.

### Current and forecast handling

The Weather state includes current temperature, humidity when supplied, precipitation probability, UV index, hourly forecasts, and active warning descriptions. The OLED keeps this concise, while the browser and API retain the fuller data set.

- Current outdoor conditions use the ECCC observation when available
- A nearest-hour forecast can fill an unavailable current field
- Missing data remains unavailable instead of being displayed as zero
- A real `0°C` reading is preserved as zero
- Forecasts on a later date include a compact date line
- Specific-time panels roll into the next day after the selected hour has passed
- Weather refreshes occur about every 15 minutes
- Saving a new source or panel configuration requests an immediate refresh
- Switching cities clears prior-city display data and prevents restoration of an unrelated cache
- ECCC requests use HTTP/1.1 with an IPv4 retry for common transport failures

### Weather icons

ECCC icon codes are normalized into compact 32 x 32 OLED sprites, including:

- Clear
- Partly cloudy
- Cloudy
- Rain
- Storm
- Snow
- Wind
- Fog

Unknown or unsupported codes use a safe fallback. The Weather Activity page records source icon codes, selected sprites, refresh results, and substitutions.

### Weather warnings

Active ECCC warning descriptions can replace the normal date in the OLED footer when music metadata is not being displayed.

- Multiple active warnings retain source order
- Ended, expired, duplicate, and empty records are ignored
- Short warnings remain centred for at least 12 seconds
- Long warnings scroll from right to left
- An active marquee completes its full cycle before refreshed warning data is applied
- Warning text cannot jump or restart midway through a pass
- Multiple warnings rotate only at clean display boundaries
- Music title and artist retain priority while a song is playing

An optional warning chime can announce a genuinely new warning description. Repeated refreshes of the same warning remain silent. The chime:

- Uses the global volume, capped at 55 percent
- Can be disabled completely
- Can be silenced during bedtime
- Is skipped while an alarm or music is already playing

## Alarms

The clock provides seven alarm slots.

Each alarm supports:

- On or off state
- Time of day
- Independent weekday selection
- A specific uploaded song or a random playable song
- Starting volume
- Final volume
- Gradual volume ramp while the alarm plays

Alarm audio repeats until dismissed or until the 30-minute safety limit is reached. If an uploaded song is missing or cannot be played, the protected built-in alarm remains available as a fallback.

The clock also reports:

- The next scheduled alarm
- Friendly timing such as `Tomorrow at 7:00 AM`
- The next alarm ID and exact timestamp through the API
- The last alarm whose decoder and audio device opened successfully
- A prominent warning when Linux time or NTP synchronization is not valid

Alarms may be dismissed from the touch sensor or the web interface.

## Music and audio

Music is uploaded and managed from the browser.

### Upload and preparation

- Upload 1 through 32 MP3 files in one selection
- Sequential preparation queue with progress reporting
- Uploads blocked while another batch is queued or processing
- Clear waiting files without interrupting the song currently being prepared
- Native in-process MP3 decoding and re-encoding
- No shell command or external transcoder is launched

The recommended profile is intended for a small 4-ohm, 3-watt mono speaker:

```text
Channels:      Mono
Bitrate:       96 kbps CBR
Sample rate:   44.1 kHz
Low-pass:      16 kHz
```

Optional controls allow 64, 96, 128, or 160 kbps; 32 or 44.1 kHz; and 12, 16, or 18 kHz low-pass limits.

### Library and metadata

- Play, stop, and delete individual songs
- Delete all user music
- Global volume control
- Album, year, track, and genre display when present
- Duration, bitrate, sample rate, channel count, MPEG layer, and file size details
- Title and artist shown on the OLED while playing
- Smooth marquee for long metadata
- Valid UTF-8 decoding
- Common accented Latin characters handled cleanly
- Malformed sequences skipped safely
- Unsupported OLED glyphs ignored rather than replaced byte by byte
- Original unfiltered metadata retained in the API and browser

Deleting a song resets alarms that selected it to Random. The built-in alarm and warning chime are stored outside the user music library and cannot be removed by the delete-all action.

## Touch controls

The TTP223B sensor provides useful controls without requiring a phone or browser.

| Touch action | Result |
| --- | --- |
| Short touch while audio is playing | Stops music or dismisses the alarm |
| Hold at least 3 seconds, then release before 8 seconds | Plays random music |
| Hold continuously for 8 seconds | Opens network diagnostics |
| Touch while diagnostics are open | Returns to the clock |

The network screen displays:

- Wi-Fi SSID
- Signal percentage
- Signal level in dBm
- IPv4 address
- Hostname

It refreshes every two seconds and closes automatically after 30 seconds.

## Web interface

The browser interface is available on the local network at:

```text
http://<clock-ip>:8080/
```

It is organized into a small set of pages rather than exposing every internal setting at once.

### Home

- Exact 256 x 64 live framebuffer preview
- Clock, audio, next alarm, bedtime, indoor sensor, and Weather summary
- Return to Clock
- Play Music
- Turn Screen Off
- Stop Music or Dismiss Alarm
- Expandable clock details

### Alarms

- Configure all seven alarms
- Select days, time, song, and volume ramp
- See whether each alarm is on or off

### Music

- Upload MP3 batches
- Watch preparation progress
- Browse metadata and technical details
- Play, stop, delete, or clear the library
- Set normal volume

### Weather

- View live AHT10 status
- Change the complete ECCC GeoMet source URL
- Restore the Calgary source preset
- Configure each of the three Weather panels
- Configure the warning chime
- Review Weather refresh activity and icon mapping

### Display

- Name the clock
- Select browser preview colour
- Configure bedtime dimming
- Select 12-hour or 24-hour time
- Choose clock and INSIDE fonts
- Upload or remove fonts
- Turn the OLED off or return to the clock

### System

- Product and software versions
- Uptime and current clock time
- IP address, hostname, SSID, interface, and Wi-Fi signal
- NTP and system-time validity
- Raspberry Pi model, serial, revision, and inventory ID
- Operating system, kernel, architecture, and compile time
- CPU temperature
- Root and boot filesystem details
- Used, available, and total storage
- Music, font, and configuration storage use
- SD-card product, manufacturer, serial, manufacture date, capacity, and CID
- OLED, touch, audio, alarm, sensor, Weather, core, and API health
- Weather source, service version, last result, and last message
- Backup and restore
- Downloadable diagnostic report
- OpenAPI specification and capability list

### Recent Activity

The activity page provides a practical history for support and troubleshooting. Logs can also be inspected through `journalctl` on the Raspberry Pi.

## Backup and restore

The System page can download a ZIP backup containing:

- Clock settings
- Alarm settings
- Uploaded fonts
- Weather source
- Weather panel selections

Restore is staged and validated before any live file is replaced. It uses strict file and path allowlists, size limits, CRC checks, and rollback handling.

The following are intentionally not replaced:

- User music
- Logs
- Weather caches
- Generated files
- Live AHT10 sensor state

Do not remove power while a restore is being applied.

## Diagnostics and maintenance

The clock is intended to be maintainable without attaching a monitor.

Useful built-in tools include:

- OLED network diagnostic screen
- Live System page
- NTP and time-health warnings
- Last successful alarm record
- Weather activity history
- Recent activity log
- Downloadable plain-text diagnostic report
- API health and capability endpoints
- systemd service status and journal logs

The installer preserves runtime settings, Weather configuration, uploaded fonts, and user music during upgrades.

## Architecture

The runtime is separated into three native C components:

### `mk-piclock-core`

Owns the hardware and time-sensitive work:

- OLED rendering
- Touch input
- AHT10 polling
- Audio playback
- Alarm scheduling
- Bedtime brightness
- Persistent clock settings

### `mk-piclock-api`

Provides:

- Web interface
- HTTP API
- MP3 upload and processing
- Font management
- Backup and restore
- Diagnostics
- Asset management

### `mk-piclock-weather`

Provides:

- ECCC GeoMet retrieval
- Current-condition normalization
- Forecast selection
- Daily low, high, and POP calculation
- Icon mapping
- Warning extraction
- Weather cache and activity records

The core and API communicate through a private Unix `SOCK_SEQPACKET` socket. The services run under separate restricted users with systemd hardening, limited writable paths, no added Linux capabilities, and automatic restart.

## Hardware

The production build supports:

- Raspberry Pi with a 40-pin GPIO header
- 256 x 64 SSD1322 OLED using SPI0
- MAX98357A I2S amplifier
- One 4 to 8-ohm speaker
- TTP223B capacitive touch sensor
- AHT10 temperature and humidity sensor using I2C1
- Regulated 5 V power supply suitable for the Pi, OLED, amplifier, and speaker load

See [`pinouts.md`](pinouts.md) for the complete wiring table, voltage rules, GPIO map, and pre-power checks.

## Software requirements

- Debian 13 or a compatible distribution with libgpiod 2.x
- systemd
- SPI0
- I2C1
- MAX98357A device-tree overlay
- Network access for ECCC Weather and NTP
- A user with `sudo` access for installation

The required Raspberry Pi boot configuration is documented in one place: [`install.md`](install.md).

## Quick installation

Install the dependencies and complete the boot-interface steps in `install.md`, then run:

```bash
unzip mk-clock-adult-1.2.62.zip
cd mk-clock-adult-1.2.62
make clean
make -j2
make install
```

Do not run `sudo make install`. The Makefile requests privilege only for the installation steps that require it.

Check the installed services:

```bash
systemctl --no-pager --full status \
  mk-piclock-core.service \
  mk-piclock-api.service \
  mk-piclock-weather.path \
  mk-piclock-weather.timer
```

Then open:

```text
http://<clock-ip>:8080/
```

For full dependencies, Raspberry Pi boot settings, build steps, verification, logging, troubleshooting, and uninstall instructions, see [`install.md`](install.md).

## Moving the clock to another Wi-Fi network

The clock uses the Linux network configuration already present on the Raspberry Pi. When NetworkManager is managing `wlan0`, a second profile can be prepared before moving it:

```bash
sudo nmcli connection add \
  type wifi \
  ifname wlan0 \
  con-name "New Home" \
  ssid "NEW_SSID"

sudo nmcli connection modify "New Home" \
  wifi-sec.key-mgmt wpa-psk \
  wifi-sec.psk 'NEW_PASSWORD' \
  connection.autoconnect yes \
  connection.autoconnect-priority 100 \
  connection.autoconnect-retries 0 \
  ipv4.method auto \
  ipv6.method auto
```

Verify the saved profiles:

```bash
nmcli -f NAME,TYPE,AUTOCONNECT,AUTOCONNECT-PRIORITY connection show
```

Do not activate the new profile while the destination network is unavailable. Shut the clock down cleanly before transport:

```bash
sudo poweroff
```

At the new location, power it on, allow time for NetworkManager and NTP to connect, then use the eight-second touch hold to view its new IP address.

## HTTP API

The local API is available at:

```text
http://<clock-ip>:8080/api/v1
```

It provides status, capabilities, health, diagnostics, alarms, audio, music, fonts, display settings, Weather configuration, Weather updates, activity logs, backup, restore, and display controls.

Useful discovery routes:

```http
GET /api/v1
GET /api/v1/capabilities
GET /api/v1/health
GET /api/v1/status
GET /api/v1/diagnostics
GET /api/v1/openapi.json
```

See [`ADDON_API.md`](ADDON_API.md) and [`api/openapi-v1.json`](api/openapi-v1.json) for details.

## Security and network placement

The web interface and API use plain HTTP and do not provide login authentication in this adult build. Keep the clock on a trusted home, IoT, or management network. Do not expose port 8080 directly to the internet.

The runtime services use separate restricted accounts and systemd sandboxing, but local network access remains the primary control for the browser interface.

## Storage

Persistent clock data is stored primarily under:

```text
/opt/mk-piclock/assets/music
/opt/mk-piclock/assets/fonts
/opt/mk-piclock/config
/var/lib/mk-piclock-weather
```

Runtime sockets are stored under:

```text
/run/mk-piclock
```

## Known limitations

- Accurate time after startup depends on Linux time restoration or NTP. The clock has no battery-backed real-time clock in the standard build.
- Weather and warning updates require network access to the selected ECCC source.
- The web interface is intended for a trusted local network and has no built-in login.
- The physical OLED colour is determined by the installed panel.
- A small amplifier click when playback starts may occur with the MAX98357A hardware.
- Indoor readings can be biased by poor sensor placement or enclosure heat.
- The Weather service is designed around ECCC GeoMet city feeds.
- Manual wiring changes must be made with power disconnected.


## Validation and release integrity

The project includes regression coverage for the parts most likely to cause an unpleasant bedside surprise:

- Alarm scheduling, safety timeout, and time-health reporting
- OLED clock and header alignment across different fonts
- AHT10 parsing and INSIDE panel states
- Font discovery, upload, selection, deletion, and fallback
- Weather panel configuration and missing-data handling
- TODAY low, high, and precipitation calculations
- Weather source changes, warning rotation, and marquee boundaries
- Backup creation, restore validation, rollback, and exclusions
- System diagnostics and downloadable report fields
- Browser module syntax and dashboard formatting
- OpenAPI and JSON validity
- Release version consistency and obsolete-asset checks

Release archives contain source rather than prebuilt foreign-architecture ELF files. The Weather service is cleaned and rebuilt on the target system during installation.

## Version compatibility

```text
Product:       mk-clock-adult-1.2.62
HTTP API:      1.44
Private IPC:   27
Weather:       Native C 2.0.14
Inside sensor: Native AHT10
```

Install the core, API, web files, and Weather service from the same release.

## Documentation

- [`install.md`](install.md): dependencies, boot configuration, build, installation, verification, logs, and troubleshooting
- [`pinouts.md`](pinouts.md): complete hardware wiring and electrical rules
- [`WEATHER_LAYOUT.md`](WEATHER_LAYOUT.md): OLED panel geometry and rendering rules
- [`WEATHER_API.md`](WEATHER_API.md): Weather update format and warning behaviour
- [`ADDON_API.md`](ADDON_API.md): local HTTP API overview
- [`api/openapi-v1.json`](api/openapi-v1.json): complete OpenAPI schema
- [`CHANGELOG.md`](CHANGELOG.md): version history
- [`RELEASE_NOTES.md`](RELEASE_NOTES.md): release-specific changes
- [`CODE_AUDIT.md`](CODE_AUDIT.md): current audit scope and validation

## A small closing note

mk-clock-adult is intentionally practical. It uses ordinary Raspberry Pi hardware, official public Weather data, local controls, and readable native code. It will not do everything, but it aims to do the bedside-clock jobs well: show the time clearly, provide useful conditions without reaching for a phone, play music, and ring when it is supposed to.
