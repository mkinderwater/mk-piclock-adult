# Changelog

## mk-clock-adult-1.2.40

- Consolidated every required Raspberry Pi `config.txt` modification into one authoritative `install.md` section.
- Documented the exact `[all]` block for SPI0, I2C1, onboard-audio disablement, and the MAX98357A I2S overlay.
- Added active boot-file detection, timestamped backup, conflict cleanup, one reboot sequence, and full post-boot verification.
- Removed duplicate boot-setting blocks from `pinouts.md` so installation rules cannot drift between documents.
- Added an installation-document regression test for the authoritative block and duplicate-setting prevention.
- Preserved HTTP API 1.37, private IPC 23, Native Weather 2.0.10, AHT10 behavior, panel logic, and runtime assets.

## mk-clock-adult-1.2.39

- Audited native C, JavaScript, configuration, and packaged assets for dead, duplicate, and unreachable logic.
- Removed the unused system-font filename lookup and consolidated duplicate grayscale sprite handling.
- Consolidated Weather panel configuration parsing into one shared, strict implementation used by the API and Weather daemon.
- Added explicit per-panel temperature availability so missing data renders as `--°` without changing a valid `0°C` reading.
- Removed fabricated startup weather values and changed omitted availability fields to unavailable.
- Fixed unchecked default-route parsing identified by Clang static analysis.
- Removed duplicate ROOM PNG sources, obsolete previews, and the contact sheet from runtime assets.
- Added missing-data, strict-config, browser-sprite, and dead-asset regression checks.
- Increased HTTP API to 1.37, private IPC to 23, and Native Weather to 2.0.10.

## mk-clock-adult-1.2.38

- Made all three OLED weather panels independently configurable as ROOM, OUTSIDE, or 1 to 48 hours ahead.
- ROOM panels use the AHT10 reading and room-sensor state image regardless of panel position.
- OUTSIDE panels use resolved current ECCC conditions; forecast panels use the selected hourly offset.
- Allowed duplicate panel selections, including repeated ROOM, OUTSIDE, or matching forecast offsets.
- Added migration from the 1.2.37 two-panel configuration to the new three-panel format.
- Added explicit panel kind through the Weather API, status JSON, and private IPC.
- Increased HTTP API to 1.36, private IPC to 22, and Native Weather to 2.0.9.

## mk-clock-adult-1.2.37

- Fixed the Dashboard `At a glance` Weather tile showing `0°C · 0%` when an ECCC location has an empty `currentConditions` object.
- Current outside temperature now uses the ECCC observation when available and the nearest hourly forecast otherwise.
- Added explicit temperature availability, hourly-fallback source, and humidity availability fields to the Weather API and private IPC.
- Changed the Weather summary percentage to labelled precipitation probability and stopped presenting missing humidity as zero.
- Detailed current weather identifies nearest-hour forecast fallback and omits humidity when unavailable.
- Increased HTTP API to 1.35, private IPC to 21, and Native Weather to 2.0.8.

## mk-clock-adult-1.2.36

- Rebuilt `pinouts.md` with a corrected 40-pin header map, clear voltage rules, complete interface ownership, and separate pre-power and post-boot checks.
- Corrected the diagram so the TTP223B and AHT10 3.3 V distribution feed is on physical pin 17; physical pin 18 is unused.
- Documented persistent I2C1 enablement through `raspi-config`, the `/boot/firmware/config.txt` fallback, reboot requirements, and verification of `/dev/i2c-1`.
- Added a `make install` I2C preflight. It rejects a saved disabled state on Raspberry Pi OS and rejects systems where `/dev/i2c-1` is unavailable.
- Preserved HTTP API 1.34, private IPC 20, Native Weather 2.0.7, and the 1.2.35 AHT10 behavior.

## mk-clock-adult-1.2.35

- Added a native AHT10 I2C temperature and humidity driver to the clock core.
- ROOM now uses local AHT10 readings and never displays an outdoor weather icon or temperature.
- Added normal, starting, stale, and error ROOM sensor sprites in validated 32 x 32 four-level RAW format with PNG previews.
- Added stale-reading retention, unavailable-state handling, configurable polling, and detailed room-sensor logging.
- Added `room_sensor` state to `GET /api/v1/status` and exposed it on the Dashboard and Weather GUI pages.
- Added I2C group, udev, service, wiring, install, and troubleshooting support.
- Added AHT10 decoder and sprite validation tests.
- Increased HTTP API to 1.34. Private IPC remains 20 and Native Weather remains 2.0.7.

## mk-clock-adult-1.2.34

- Confirmed the ECCC GeoJSON parser uses `properties.currentConditions.temperature.value.en`, `condition.en`, and `iconCode.value` correctly.
- Added field-level ROOM and OUTSIDE display fallbacks for locations whose `currentConditions` object is empty.
- Missing realtime temperature and icon values now use the nearest valid hourly forecast values without altering normalized source observations.
- Daily highs and lows are not used as current-condition fallbacks.
- Added regression coverage for empty current conditions and increased Native Weather to 2.0.7.
- HTTP API remains 1.33 and private IPC remains 20.

## mk-clock-adult-1.2.33

- Removed non-runtime Weather PNG masters and preview sheets; the installed OLED uses only the 49 validated RAW sprites.
- Removed an ignored footer-marquee local, a duplicate GUI asset-version constant, and an unused CSS selector.
- Corrected the Weather API documentation to reflect music-over-warning footer priority.

- Promoted the completed adult release line to product version 1.2.33.
- Added the lowercase `install.md` installation, upgrade, verification, troubleshooting, and uninstall guide.
- Retained the 1.2.32 clock centring, UTF-8 metadata, ended-alert handling, and System GUI network diagnostics changes.
- HTTP API remains 1.33, private IPC remains 20, and native Weather remains 2.0.6.

## mk-clock-adult-1.2.32

- The System GUI now includes the kid-clock Network card with IPv4 address, hostname, SSID, interface, Wi-Fi signal percentage and dBm, and NTP synchronization state.
- Added `GET /api/v1/diagnostics` for the network data and increased the HTTP API to 1.33.
- Added UTF-8-aware OLED filtering for music title and artist metadata.
- Valid accented Latin characters use the existing OLED glyph mapping; malformed sequences and unsupported glyphs are skipped safely.
- Filtered title and artist are combined only for the OLED; original metadata remains available through the API and web GUI.
- Changed the adult clock to fixed H:MM and HH:MM grids using widest-digit slots, right-aligned digit ink, and a centred colon slot.
- Single-digit 12-hour times use the centred H:MM grid; two-digit and 24-hour times use the centred HH:MM grid, preventing minute changes from moving the clock.
- Retained the existing 2-pixel horizontal adjustment and aligned the location label to the same centre line.
- Ended ECCC warning records are now ignored immediately, even when their cleanup expiry remains in the future.
- Preserved Weather geometry, separators, seconds line, date position, footer priority and private IPC 20. Native Weather is 2.0.6.

## mk-clock-adult-1.2.31

- Changed the OLED footer priority to music metadata, then active Weather warning, then the normal date.
- Active music now overrides the Weather warning marquee.
- The warning returns automatically when playback stops.
- HTTP API remains 1.32, private IPC remains 20, and native Weather remains 2.0.5.

## mk-clock-adult-1.2.30

- Replaced the configurable Weather-panel heading `CURRENT` with `OUTSIDE`.
- Changed the Weather-page option label from `Current` to `Outside`.
- Preserved the internal `current` mode and API value for compatibility.
- Fixed OLED network diagnostics showing `Unavailable` for Wi-Fi and IP by permitting the core service to open its required `AF_INET` ioctl socket.
- Signal readings were unaffected because they are read directly from `/proc/net/wireless`.
- HTTP API remains 1.32, private IPC remains 20, and native Weather is 2.0.5.

## mk-clock-adult-1.2.29

- Removed three redundant touch-state reset assignments after sensor release.
- Each removed value was overwritten at the next touch press before being read.
- Touch and network-diagnostic behavior remain unchanged.
- HTTP API remains 1.32, private IPC remains 20, and native Weather remains 2.0.4.

## mk-clock-adult-1.2.28

- Added the proven kid-clock OLED network diagnostic screen.
- An uninterrupted 8-second touch hold displays Wi-Fi SSID, signal percentage and dBm, IPv4 address, and hostname.
- Diagnostics refresh every 2 seconds, time out after 30 seconds, and close on the next touch.
- Changed the existing 3-second random-music gesture to run on release so an 8-second diagnostic hold is not intercepted at 3 seconds.
- HTTP API remains 1.32, private IPC remains 20, and native Weather remains 2.0.4.

## mk-clock-adult-1.2.27

- Corrected ECCC warning-title parsing for the citypageweather-realtime feed.
- ECCC `type` values such as `watch` are now combined with the hazard in `description`.
- Example: `YELLOW WATCH - SEVERE THUNDERSTORM` now displays as `SEVERE THUNDERSTORM WATCH` rather than `WATCH`.
- Increased the native Weather service to 2.0.4. HTTP API remains 1.32 and private IPC remains 20.

## mk-clock-adult-1.2.26

- Added the protected kid-clock message chime asset byte-for-byte as the Weather warning chime.
- Added an optional Weather warning chime setting to the Weather page.
- Added an independent option to silence warning chimes during the configured bedtime window.
- Chimes only when a new non-empty warning `type` appears or changes; repeated refreshes of the same warning remain silent.
- Warning chimes use global volume capped at 55% and are skipped while music or an alarm is playing.
- Increased the HTTP API to 1.32 and private IPC to 20. Native Weather remains 2.0.3.

## mk-clock-adult-1.2.25

- Added active ECCC warning types to the Weather update pipeline.
- Active warning types replace the date and music metadata in the OLED footer.
- Warning text uses the proven kid-clock marquee; only the warning `type` is displayed.
- Restored song title and artist display in the same marquee region when no warning is active.
- Increased the HTTP API to 1.31, private IPC to 19, and native Weather service to 2.0.3.

## mk-clock-adult-1.2.24

- Corrected the seconds-line request from 3 mm to 3 clear pixels on each side.
- Accounted for X=102 being occupied by the vertical Weather separator.
- Set the seconds line to X=3..98, leaving exactly X=0..2 and X=99..101 clear.
- Remapped seconds 0 through 59 across the corrected line endpoints.

## mk-clock-adult-1.2.23

- Inset the animated seconds line by 10 pixels, approximately 3 mm, on both the left and right sides.
- Remapped seconds 0 through 59 across the shortened line so the dark pixel still reaches both endpoints.

## mk-clock-adult-1.2.22

- Moved the clock seconds-position line up 2 pixels, from Y=52 to Y=50.
- Kept the clock date at Y=54 and left all other dashboard geometry unchanged.

## mk-clock-adult-1.2.21

- Added a horizontal seconds-position line above the clock date.
- Matched the line brightness to the Weather panel separators.
- Added one moving dark pixel that advances from left to right over seconds 0 through 59 and resets each minute.

## mk-clock-adult-1.2.20

- Aligned the Weather location label to the same horizontal centre as the shifted clock digits.

## mk-clock-adult-1.2.19

- Moved the Weather location label down 2 pixels.
- Moved the clock date up 2 pixels.
- Shifted the clock left for better centring in the left panel.

## mk-clock-adult-1.2.17

- Added the configured ECCC Weather location above the clock using centred compact 3x5 text.
- Moved the main clock time to Y=7..48 so its vertical centre aligns with the Weather icons at Y=28.
- Moved the clock-region date to Y=56..62.
- Added the Weather location to `POST /api/v1/weather`, the private IPC update, and `/api/v1/status`.
- Increased the HTTP API to 1.30, private IPC to 18, and native Weather service to 2.0.2.

## mk-clock-adult-1.2.16

- Restored the OLED degree mark while continuing to omit the redundant `C` unit letter.
- OLED Weather temperatures now render as `18°` or `18° (20%)`.
- Kept the HTTP API and private IPC versions unchanged.

## mk-clock-adult-1.2.15

- Added `current` as a forecast-panel selection mode for realtime outside conditions.
- Replaced the first Weather panel heading `NOW` with `ROOM` for the planned room-temperature sensor.
- Added `CURRENT` as the OLED heading for configurable panels using realtime outside conditions.
- Moved Weather panel headings down one pixel.
- Removed the redundant `C` unit suffix from OLED temperatures.
- Increased the HTTP API version to 1.29.

## mk-clock-adult-1.2.14

- Audited adult-only workarounds against kid-clock v1.9.5.
- Rebased the complete shared Music upload, queue, and multipart flow on the kid-clock implementation.
- Removed Display-form hidden-field mirroring; shared display settings now use the existing partial-update present mask.
- Restored the kid-clock system-font catalogue, stable system-font keys, browser previews, and core font resolution.
- Removed dead Music CSS compatibility aliases.
- Restored the kid-clock shared request helper after correcting form ownership and POST metadata.
- Kept adult-only Weather integration isolated from shared kid-clock code.

## mk-clock-adult-1.2.13

- Restored the proven kid-clock MP3 upload sequence.
- Music uploads now submit `new FormData(form)` directly while the file input remains enabled until `fetch()` starts.
- Removed the adult-only pre-request file-input lock that caused Safari to serialize a multipart request without file parts.
- Confirmed the protected default alarm is byte-for-byte identical to kid-clock v1.9.5.

## mk-clock-adult-1.2.12

- Fixed Music uploads that could submit zero files after the asynchronous queue check.
- Snapshot selected MP3 `File` objects immediately and build multipart data explicitly, independent of disabled GUI controls.
- Split API errors for zero files and selections above 32 files.
- Verified `assets/default-alarm.mp3` is byte-for-byte identical to the kid-clock v1.9.5 protected alarm.
- Added source and installed-file SHA-256 verification for the built-in alarm during `make install`.

## mk-clock-adult-1.2.11

- Fixed the transient `Request with GET/HEAD method cannot have body` GUI notice.
- Marked Weather source and forecast-panel forms as module-managed to prevent duplicate submission.
- Added explicit POST methods and API actions to both Weather forms.
- Hardened the shared GUI request helper so GET and HEAD requests never include a body.

## mk-clock-adult-1.2.10

- Added a future-date line beneath forecast temperature/precipitation on the OLED.
- Forecast panels remain unchanged for the current local date.
- Future dates use the selected Weather location timezone and full uppercase month format, for example `JULY 23`.
- Added `slotN_date_label` to the native Weather API/IPC payload and status output.
- Increased the HTTP API version to 1.27 and private IPC protocol version to 17.

## mk-clock-adult-1.2.9

- Fixed forecast-panel changes being saved without immediately refreshing the OLED.
- Corrected the installer so `mk-piclock-weather.path` is started, not merely enabled for the next boot.
- Made `make install` start the core, API, Weather path monitor, and Weather timer, then request an initial Weather refresh.
- Fixed duplicate Weather timezone cleanup that could double-free an inherited `TZ` value.

## mk-clock-adult-1.2.8

- Fixed Music uploads submitting zero files because the file input was disabled before `FormData` was created.
- Confirmed the GUI accepts and submits selections containing 1 through 32 MP3 files.
- Kept the existing batch queue and sequential MP3 optimization behaviour unchanged.

## mk-clock-adult-1.2.7

- Restored the shared System GUI module to the adult clock navigation.
- Restored the shared `audio-library.js` helper used by the Music interface.
- Updated Music cards to use the common audio metadata layout without restoring kid-only modules.
- Retained all PNG and RAW Weather assets plus the protected default alarm.

## mk-clock-adult-1.2.6

- Restored all 49 original 32x32 PNG Weather icon masters.
- Restored the Weather grid and pixel-art preview PNG sheets.
- Retained the 49 RAW sprites as the installed OLED runtime assets.
- Reconfirmed the protected built-in default alarm remains packaged and installed.

## mk-clock-adult-1.2.5

- Audited the adult runtime package against the original adult branch and mk-piclock 1.9.5.
- Fixed stale `errno` handling in the native Weather source-file reader.
- Added regression coverage for missing and unreadable Weather source paths.
- Corrected the documented API route list and current product references.
- Added the HTTPS certificate, timezone-data, and libgpiod 2 requirements to installation notes.
- Revalidated the protected default alarm and all required runtime assets.

## mk-clock-adult-1.2.4

- Restored `assets/default-alarm.mp3` from the kid-clock release.
- Added a protected built-in alarm fallback when no uploaded music is playable.
- Installed the default alarm outside the user-managed music directory so delete-all cannot remove it.

## mk-clock-adult-1.2.3

- Removed duplicate Weather integration source and web files.
- Removed standalone add-on scaffolding and unused Weather PNG reference assets.
- Removed unused IPC and browser CSS code.
- Simplified the native Weather installer while preserving user settings.

## mk-clock-adult-1.2.2

- Restored the standard kid-clock Makefile flow.
- Added only the native Weather build, install, clean, and uninstall steps.
- Removed the Debian provisioning and bootstrap changes from 1.2.1.

## mk-clock-adult-1.2.1

- Added an experimental fresh-Debian bootstrap installer.
- Replaced by the simpler 1.2.2 Makefile flow.

## mk-clock-adult-1.2.0

- Replaced the Python ECCC Weather service with a compiled C implementation.
- Added libcurl HTTPS transport and json-c normalization.
- Preserved source validation, forecast selection, cache restore, activity logging, OLED icon publication, and clock API updates.
- Replaced Python tests with native C regression tests.
- Removed Python Weather source and runtime dependencies.

## mk-clock-adult-1.1.0

- Updated shared GUI styling, navigation behaviour, browser icons, and cache keys from mk-piclock v1.9.5.
- Updated the alarm and activity modules while retaining adult API compatibility.
- Updated music-job cleanup, retention, and queue-slot reuse logic.
- Updated shared IPC utility reads and service hardening.
- Preserved the Weather dashboard and adult-only module set.
- Continued to exclude Messages, Lighting, Stories, Day Images, and Bedtime Images.

## mk-clock-adult-1.0.0

- Created the adult build.
- Added Weather page, weather source settings, weather activity log, and configurable forecast panels.
- Integrated the standalone ECCC GeoMet weather fetcher.
- Kept alarms, music, fonts, touch controls, live OLED preview, status, and logs.
- Removed Messages, Lighting, Stories, Day Images, Bedtime Images, and stale web modules.
- Removed old image and story runtime directories during install.
- Updated web/app branding and cache keys.
