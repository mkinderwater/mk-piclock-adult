#ifndef MK_PICLOCK_WEATHER_FRAMES_H
#define MK_PICLOCK_WEATHER_FRAMES_H

#include <stddef.h>

#define MP_WEATHER_FRAMES_FILE "/var/lib/mk-piclock-weather/weather-frames.conf"
#define MP_WEATHER_FRAME_OFFSET_MIN 1
#define MP_WEATHER_FRAME_OFFSET_MAX 48
#define MP_WEATHER_PANEL_COUNT 3

enum mp_weather_frame_mode {
    MP_WEATHER_FRAME_ROOM = 1,
    MP_WEATHER_FRAME_OUTSIDE = 2,
    MP_WEATHER_FRAME_OFFSET = 3,
    MP_WEATHER_FRAME_TIME = 4,
    MP_WEATHER_FRAME_TODAY = 5
};

struct mp_weather_frame_selection {
    enum mp_weather_frame_mode mode;
    int offset_hours;
    int time_hour;
};

struct mp_weather_frames_config {
    struct mp_weather_frame_selection slots[MP_WEATHER_PANEL_COUNT];
};

const char *mp_weather_frame_mode_name(enum mp_weather_frame_mode mode);
int mp_weather_frame_mode_parse(const char *value, enum mp_weather_frame_mode *mode);
void mp_weather_frames_defaults(struct mp_weather_frames_config *config);
int mp_weather_frames_validate(const struct mp_weather_frames_config *config,
                               char *error, size_t error_size);
int mp_weather_frames_read_path(const char *path,
                                struct mp_weather_frames_config *config,
                                char *error, size_t error_size);
int mp_weather_frames_read(struct mp_weather_frames_config *config,
                           char *error, size_t error_size);
int mp_weather_frames_serialize(const struct mp_weather_frames_config *config,
                                char *output, size_t output_size,
                                char *error, size_t error_size);
int mp_weather_frames_write(const struct mp_weather_frames_config *config,
                            char *error, size_t error_size);

#endif
