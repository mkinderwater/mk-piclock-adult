# mk-clock-adult Weather API

The native Weather service pushes one complete dashboard snapshot to:

```text
POST /api/v1/weather
Content-Type: application/x-www-form-urlencoded
```

## Current fields

| Field | Description |
|---|---|
| `location` | Weather location displayed above the OLED clock |
| `warning_type` | Highest-priority active ECCC warning type, or empty |
| `current_temperature_c` | Resolved current outside temperature, -99 to 99 |
| `current_temperature_available` | `1` only when `current_temperature_c` is valid |
| `current_temperature_is_forecast` | `1` when the current value came from the nearest hourly fallback |
| `humidity_percent` | Current observed outside relative humidity, 0 to 100 |
| `humidity_available` | `1` only when ECCC supplied current humidity |
| `precipitation_probability_percent` | Current hourly precipitation probability, 0 to 100 |
| `uv_index` | Current hourly UV index, 0 to 99 |
| `observed_at` | Unix timestamp |

Availability defaults to `0` when omitted. This prevents absent fields from becoming believable zero readings.

## Panel fields

For each panel `N`, where `N` is 0 through 2:

| Field | Description |
|---|---|
| `slotN_kind` | `room`, `outside`, or `forecast` |
| `slotN_label` | OLED panel label |
| `slotN_date_label` | Optional future local date |
| `slotN_temperature_available` | `1` only when `slotN_temperature_c` is valid |
| `slotN_temperature_c` | Outside or forecast temperature, -99 to 99 |
| `slotN_precipitation_probability_percent` | Outside or forecast precipitation chance, 0 to 100 |
| `slotN_icon` | Outside or forecast icon name |

ROOM ignores weather temperature, precipitation, and icon fields. It renders the local AHT10 reading and sensor-state sprite. Missing OUTSIDE or forecast temperatures render as `--^`; a valid `0°C` renders as `0^`.

Supported icon names are `clear`, `partly`, `cloudy`, `rain`, `storm`, `snow`, `wind`, and `fog`. Missing or unsupported icons use the unknown-image fallback.

## Weather panel selection

All three panels are configured through `GET` and `POST /api/v1/config/weather-frames`.

- `room`: local AHT10 temperature and humidity
- `outside`: current ECCC conditions, with field-level nearest-hour fallback when required
- `offset`: hourly forecast from 1 through 48 hours ahead

Each panel is independent and selections may repeat. The default is ROOM, +3 hours, and +6 hours.

## Example

```bash
curl -X POST http://mk-piclock.local:8080/api/v1/weather \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data-urlencode 'location=Calgary' \
  --data-urlencode 'current_temperature_c=21' \
  --data-urlencode 'current_temperature_available=1' \
  --data-urlencode 'current_temperature_is_forecast=0' \
  --data-urlencode 'humidity_percent=38' \
  --data-urlencode 'humidity_available=1' \
  --data-urlencode 'precipitation_probability_percent=30' \
  --data-urlencode 'uv_index=5' \
  --data-urlencode 'slot0_kind=room' \
  --data-urlencode 'slot0_label=ROOM' \
  --data-urlencode 'slot0_date_label=' \
  --data-urlencode 'slot0_temperature_available=0' \
  --data-urlencode 'slot0_temperature_c=0' \
  --data-urlencode 'slot0_precipitation_probability_percent=0' \
  --data-urlencode 'slot0_icon=' \
  --data-urlencode 'slot1_kind=outside' \
  --data-urlencode 'slot1_label=OUTSIDE' \
  --data-urlencode 'slot1_date_label=' \
  --data-urlencode 'slot1_temperature_available=1' \
  --data-urlencode 'slot1_temperature_c=19' \
  --data-urlencode 'slot1_precipitation_probability_percent=40' \
  --data-urlencode 'slot1_icon=rain' \
  --data-urlencode 'slot2_kind=forecast' \
  --data-urlencode 'slot2_label=6PM' \
  --data-urlencode 'slot2_date_label=JULY 23' \
  --data-urlencode 'slot2_temperature_available=1' \
  --data-urlencode 'slot2_temperature_c=12' \
  --data-urlencode 'slot2_precipitation_probability_percent=0' \
  --data-urlencode 'slot2_icon=partly'
```

The complete weather state, including each panel `kind` and `temperature_available`, is returned inside the `weather` object from `GET /api/v1/status`.

## Warning Marquee

When `warning_type` is non-empty, its value replaces the normal date in the OLED footer. Playing song metadata has higher priority than a weather warning.

## Warning Chime

The Weather page provides two settings through `POST /api/v1/config/display`:

- `weather_warning_chime_enabled`
- `weather_warning_chime_during_bedtime`

A chime is considered only when `warning_type` changes to a new non-empty value. Repeated refreshes containing the same warning remain silent. The chime uses global volume capped at 55% and is skipped while music or an alarm is already playing.
