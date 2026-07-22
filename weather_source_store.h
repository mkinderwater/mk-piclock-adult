#ifndef MK_PICLOCK_WEATHER_SOURCE_STORE_H
#define MK_PICLOCK_WEATHER_SOURCE_STORE_H

#include <stddef.h>

#include "weather_frames.h"

#define MP_WEATHER_SOURCE_URL_MAX 2048
#define MP_WEATHER_SOURCE_FILE "/var/lib/mk-piclock-weather/weather-source.url"
#define MP_WEATHER_STATUS_FILE "/var/lib/mk-piclock-weather/status.json"
#define MP_WEATHER_ACTIVITY_FILE "/var/lib/mk-piclock-weather/activity.json"
#define MP_WEATHER_STATUS_JSON_MAX 8192
#define MP_WEATHER_ACTIVITY_JSON_MAX 49152
#define MP_WEATHER_SOURCE_DEFAULT \
    "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json"

int mp_weather_source_validate(
    const char *input,
    char *canonical,
    size_t canonical_size,
    char *error,
    size_t error_size
);

int mp_weather_source_read(
    char *url,
    size_t url_size,
    char *error,
    size_t error_size
);

int mp_weather_source_write(
    const char *input,
    char *canonical,
    size_t canonical_size,
    char *error,
    size_t error_size
);

int mp_weather_status_read_json(
    char *json,
    size_t json_size,
    char *error,
    size_t error_size
);

int mp_weather_activity_read_json(
    char *json,
    size_t json_size,
    char *error,
    size_t error_size
);

#endif
