# mk-clock-adult HTTP API

The GUI and local add-ons use the HTTP API on port 8080.

```text
http://<clock-ip>:8080/api/v1
```

Use it only on a trusted local network.

## Discovery And Status

```http
GET /api/v1
GET /api/v1/capabilities
GET /api/v1/openapi.json
GET /api/v1/health
GET /api/v1/status
GET /api/v1/diagnostics
```

## Music And Fonts

```http
GET  /api/v1/assets/music
POST /api/v1/assets/music/upload
GET  /api/v1/assets/music/jobs
POST /api/v1/assets/music/jobs/clear
POST /api/v1/assets/music/delete
POST /api/v1/assets/music/delete-all
GET  /api/v1/assets/fonts
GET  /api/v1/assets/fonts/file
POST /api/v1/assets/fonts/upload
POST /api/v1/assets/fonts/delete
```

Music upload accepts 1 to 32 MP3 files in one selection. New uploads are rejected while any song is queued or processing.

The font list includes built-in OLED fonts, uploaded fonts, and installed Linux fonts. Linux fonts use stable `system:<hash>` keys. Supply `file=` or `key=` to `/api/v1/assets/fonts/file`.

## Display And Configuration

```http
POST /api/v1/display/action
GET  /api/v1/display/preview
POST /api/v1/display/brightness-preview
POST /api/v1/config/alarms
POST /api/v1/config/audio
POST /api/v1/config/personalization
POST /api/v1/config/display
GET  /api/v1/config/weather-source
POST /api/v1/config/weather-source
GET  /api/v1/config/weather-frames
POST /api/v1/config/weather-frames
```

Each of the three panel modes is `room`, `outside`, or `offset`. `room` uses AHT10 data, `outside` uses current ECCC conditions with field-level nearest-hour fallback, and `offset` selects an hourly forecast 1 through 48 hours ahead. Panels may repeat a source.

## Weather

```http
POST /api/v1/weather
GET  /api/v1/weather/activity
```

The weather service posts the ECCC location name and normalized GeoMet data to `/api/v1/weather`. The location and current weather state are included in `/api/v1/status`. Future forecast slots may include `date_label`, such as `JULY 23`, when the selected forecast falls on a later local date. Each status slot includes `kind` and `temperature_available`. Missing panel temperatures remain unavailable and render as `--°`, while ROOM panels are rendered from the separate native AHT10 `room_sensor` object.

## Activity

```http
GET  /api/v1/logs
POST /api/v1/logs/clear
```

## Compatibility

```text
HTTP API:    1.37
Private IPC: 23
```

## Network diagnostics

`GET /api/v1/diagnostics` returns the clock hostname, active IPv4 interface and address, Wi-Fi SSID and signal strength, and NTP synchronization state.
