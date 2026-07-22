#define _GNU_SOURCE

#include "weather_frames.h"
#include "io_helpers.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct parsed_frame_selection {
    enum mp_weather_frame_mode mode;
    int offset_hours;
    int legacy_hour;
    int mode_seen;
    int offset_seen;
    int legacy_hour_seen;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0) return;
    snprintf(error, error_size, "%s", message ? message : "weather panel error");
}

static char *trim_ascii(char *value)
{
    if (!value) return value;
    while (*value && isspace((unsigned char)*value)) value++;
    char *end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return value;
}

static int parse_int_range(const char *value, int minimum, int maximum, int *output)
{
    if (!value || !*value || !output) return -1;
    char *end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || !end || *trim_ascii(end) != '\0' ||
        parsed < minimum || parsed > maximum) return -1;
    *output = (int)parsed;
    return 0;
}

const char *mp_weather_frame_mode_name(enum mp_weather_frame_mode mode)
{
    switch (mode) {
        case MP_WEATHER_FRAME_ROOM: return "room";
        case MP_WEATHER_FRAME_OUTSIDE: return "outside";
        case MP_WEATHER_FRAME_OFFSET: return "offset";
        default: return "invalid";
    }
}

int mp_weather_frame_mode_parse(const char *value, enum mp_weather_frame_mode *mode)
{
    if (!value || !mode) return -1;
    if (strcmp(value, "room") == 0) {
        *mode = MP_WEATHER_FRAME_ROOM;
        return 0;
    }
    if (strcmp(value, "outside") == 0 || strcmp(value, "current") == 0) {
        *mode = MP_WEATHER_FRAME_OUTSIDE;
        return 0;
    }
    if (strcmp(value, "offset") == 0 || strcmp(value, "time") == 0) {
        *mode = MP_WEATHER_FRAME_OFFSET;
        return 0;
    }
    return -1;
}

void mp_weather_frames_defaults(struct mp_weather_frames_config *config)
{
    if (!config) return;
    *config = (struct mp_weather_frames_config){
        .slots = {
            {MP_WEATHER_FRAME_ROOM, 1},
            {MP_WEATHER_FRAME_OFFSET, 3},
            {MP_WEATHER_FRAME_OFFSET, 6}
        }
    };
}

int mp_weather_frames_validate(const struct mp_weather_frames_config *config,
                               char *error, size_t error_size)
{
    if (!config) {
        set_error(error, error_size, "weather panel settings are invalid");
        return -1;
    }
    for (int index = 0; index < MP_WEATHER_PANEL_COUNT; index++) {
        const struct mp_weather_frame_selection *selection = &config->slots[index];
        if (selection->mode != MP_WEATHER_FRAME_ROOM &&
            selection->mode != MP_WEATHER_FRAME_OUTSIDE &&
            selection->mode != MP_WEATHER_FRAME_OFFSET) {
            set_error(error, error_size,
                      "weather panel mode must be room, outside, or offset");
            return -1;
        }
        if (selection->offset_hours < MP_WEATHER_FRAME_OFFSET_MIN ||
            selection->offset_hours > MP_WEATHER_FRAME_OFFSET_MAX) {
            set_error(error, error_size,
                      "weather panel offset must be between 1 and 48 hours");
            return -1;
        }
    }
    return 0;
}

static int parse_key(const char *key, int *slot, const char **field)
{
    if (!key || strncmp(key, "slot", 4) != 0 ||
        key[4] < '1' || key[4] > '3' || key[5] != '_' || key[6] == '\0') {
        return -1;
    }
    *slot = key[4] - '1';
    *field = key + 6;
    return 0;
}

int mp_weather_frames_read_path(const char *path,
                                struct mp_weather_frames_config *config,
                                char *error, size_t error_size)
{
    if (!path || !*path || !config) {
        set_error(error, error_size, "weather panel input is invalid");
        return -1;
    }
    mp_weather_frames_defaults(config);

    FILE *file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT) return 0;
        char message[192];
        snprintf(message, sizeof(message), "cannot read weather panel settings: %s",
                 strerror(errno));
        set_error(error, error_size, message);
        return -1;
    }

    struct parsed_frame_selection parsed[MP_WEATHER_PANEL_COUNT] = {0};
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        size_t line_length = strlen(line);
        if (line_length == sizeof(line) - 1 && line[line_length - 1] != '\n' && !feof(file)) {
            fclose(file);
            set_error(error, error_size, "weather panel settings contain an overlong line");
            return -1;
        }
        char *entry = trim_ascii(line);
        if (!*entry || *entry == '#') continue;
        char *separator = strchr(entry, '=');
        if (!separator) {
            fclose(file);
            set_error(error, error_size, "weather panel settings contain an invalid line");
            return -1;
        }
        *separator = '\0';
        char *key = trim_ascii(entry);
        char *value = trim_ascii(separator + 1);
        int slot = 0;
        const char *field = NULL;
        if (parse_key(key, &slot, &field) != 0) {
            fclose(file);
            set_error(error, error_size, "weather panel settings contain an unknown key");
            return -1;
        }

        if (strcmp(field, "mode") == 0) {
            if (parsed[slot].mode_seen ||
                mp_weather_frame_mode_parse(value, &parsed[slot].mode) != 0) {
                fclose(file);
                set_error(error, error_size,
                          "weather panel settings contain a duplicate or invalid mode");
                return -1;
            }
            parsed[slot].mode_seen = 1;
        } else if (strcmp(field, "offset_hours") == 0) {
            if (parsed[slot].offset_seen ||
                parse_int_range(value, MP_WEATHER_FRAME_OFFSET_MIN,
                                MP_WEATHER_FRAME_OFFSET_MAX,
                                &parsed[slot].offset_hours) != 0) {
                fclose(file);
                set_error(error, error_size,
                          "weather panel offset must be between 1 and 48 hours");
                return -1;
            }
            parsed[slot].offset_seen = 1;
        } else if (strcmp(field, "hour") == 0) {
            if (parsed[slot].legacy_hour_seen ||
                parse_int_range(value, 0, 23, &parsed[slot].legacy_hour) != 0) {
                fclose(file);
                set_error(error, error_size,
                          "legacy weather panel hour must be between 0 and 23");
                return -1;
            }
            parsed[slot].legacy_hour_seen = 1;
        } else {
            fclose(file);
            set_error(error, error_size, "weather panel settings contain an unknown key");
            return -1;
        }
    }
    if (ferror(file)) {
        int saved_errno = errno;
        fclose(file);
        char message[192];
        snprintf(message, sizeof(message), "cannot read weather panel settings: %s",
                 strerror(saved_errno));
        set_error(error, error_size, message);
        return -1;
    }
    fclose(file);

    const int new_format = parsed[2].mode_seen || parsed[2].offset_seen ||
                           parsed[2].legacy_hour_seen;
    if (new_format) {
        for (int index = 0; index < MP_WEATHER_PANEL_COUNT; index++) {
            if (!parsed[index].mode_seen || parsed[index].legacy_hour_seen) {
                set_error(error, error_size,
                          "weather panel settings are incomplete or mix legacy fields");
                return -1;
            }
            config->slots[index].mode = parsed[index].mode;
            if (parsed[index].offset_seen)
                config->slots[index].offset_hours = parsed[index].offset_hours;
            else if (parsed[index].mode == MP_WEATHER_FRAME_OFFSET) {
                set_error(error, error_size,
                          "offset weather panels require offset_hours");
                return -1;
            }
        }
    } else {
        /* 1.2.37 stored only panels 2 and 3. Panel 1 was fixed ROOM. */
        for (int index = 0; index < 2; index++) {
            if (!parsed[index].mode_seen || !parsed[index].offset_seen ||
                !parsed[index].legacy_hour_seen) {
                set_error(error, error_size,
                          "legacy weather panel settings are incomplete");
                return -1;
            }
            config->slots[index + 1].mode = parsed[index].mode;
            config->slots[index + 1].offset_hours = parsed[index].offset_hours;
        }
    }

    return mp_weather_frames_validate(config, error, error_size);
}

int mp_weather_frames_read(struct mp_weather_frames_config *config,
                           char *error, size_t error_size)
{
    return mp_weather_frames_read_path(MP_WEATHER_FRAMES_FILE, config,
                                       error, error_size);
}

int mp_weather_frames_serialize(const struct mp_weather_frames_config *config,
                                char *output, size_t output_size,
                                char *error, size_t error_size)
{
    if (!output || output_size == 0 ||
        mp_weather_frames_validate(config, error, error_size) != 0) {
        if (!output || output_size == 0)
            set_error(error, error_size, "weather panel output is invalid");
        return -1;
    }
    int length = snprintf(
        output, output_size,
        "slot1_mode=%s\nslot1_offset_hours=%d\n"
        "slot2_mode=%s\nslot2_offset_hours=%d\n"
        "slot3_mode=%s\nslot3_offset_hours=%d\n",
        mp_weather_frame_mode_name(config->slots[0].mode), config->slots[0].offset_hours,
        mp_weather_frame_mode_name(config->slots[1].mode), config->slots[1].offset_hours,
        mp_weather_frame_mode_name(config->slots[2].mode), config->slots[2].offset_hours);
    if (length < 0 || (size_t)length >= output_size) {
        set_error(error, error_size, "weather panel settings are too large");
        return -1;
    }
    return length;
}

int mp_weather_frames_write(const struct mp_weather_frames_config *config,
                            char *error, size_t error_size)
{
    char payload[384];
    int length = mp_weather_frames_serialize(config, payload, sizeof(payload),
                                             error, error_size);
    if (length < 0) return -1;

    char temporary[] = "/var/lib/mk-piclock-weather/.weather-frames.conf.XXXXXX";
    int fd = mkstemp(temporary);
    if (fd < 0) {
        char message[192];
        snprintf(message, sizeof(message), "cannot create weather panel settings: %s",
                 strerror(errno));
        set_error(error, error_size, message);
        return -1;
    }

    int result = 0;
    if (mp_write_full(fd, payload, (size_t)length) != 0 ||
        fchmod(fd, 0644) != 0 || fsync(fd) != 0) result = -1;
    int saved_errno = errno;
    if (close(fd) != 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result == 0 && rename(temporary, MP_WEATHER_FRAMES_FILE) != 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result == 0 && mp_fsync_parent_directory(MP_WEATHER_FRAMES_FILE) != 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result != 0) {
        unlink(temporary);
        char message[192];
        snprintf(message, sizeof(message), "cannot save weather panel settings: %s",
                 strerror(saved_errno));
        set_error(error, error_size, message);
        errno = saved_errno;
        return -1;
    }
    return 0;
}
