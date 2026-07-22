#define _GNU_SOURCE

#include "weather_source_store.h"
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

#define MP_WEATHER_SOURCE_PREFIX \
    "https://api.weather.gc.ca/collections/citypageweather-realtime/items/"
#define MP_WEATHER_SOURCE_SUFFIX "?f=json"

static void set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) return;
    snprintf(error, error_size, "%s", message ? message : "weather source error");
}

int mp_weather_source_validate(
    const char *input,
    char *canonical,
    size_t canonical_size,
    char *error,
    size_t error_size
) {
    if (!canonical || canonical_size == 0) {
        set_error(error, error_size, "weather source output buffer is invalid");
        return -1;
    }
    canonical[0] = '\0';

    if (!input) {
        set_error(error, error_size, "GeoMet URL is required");
        return -1;
    }

    while (*input && isspace((unsigned char)*input)) input++;
    size_t length = strlen(input);
    while (length > 0 && isspace((unsigned char)input[length - 1])) length--;

    if (length == 0 || length >= MP_WEATHER_SOURCE_URL_MAX) {
        set_error(error, error_size, "GeoMet URL is empty or too long");
        return -1;
    }

    for (size_t index = 0; index < length; index++) {
        unsigned char value = (unsigned char)input[index];
        if (value < 0x20 || value == 0x7f || value == '\\' || value == '"' || value == '\'') {
            set_error(error, error_size, "GeoMet URL contains invalid characters");
            return -1;
        }
    }

    const size_t prefix_length = strlen(MP_WEATHER_SOURCE_PREFIX);
    if (length <= prefix_length ||
        strncmp(input, MP_WEATHER_SOURCE_PREFIX, prefix_length) != 0) {
        set_error(error, error_size,
            "Use an official ECCC GeoMet citypageweather-realtime item URL");
        return -1;
    }

    const char *identifier = input + prefix_length;
    size_t identifier_length = 0;
    while (prefix_length + identifier_length < length) {
        char value = identifier[identifier_length];
        if (value == '?' || value == '#') break;
        if (value == '/') {
            if (prefix_length + identifier_length + 1 == length ||
                identifier[identifier_length + 1] == '?') {
                break;
            }
            set_error(error, error_size, "GeoMet city identifier is invalid");
            return -1;
        }
        identifier_length++;
    }

    if (identifier_length < 4 || identifier[2] != '-' ||
        !islower((unsigned char)identifier[0]) ||
        !islower((unsigned char)identifier[1])) {
        set_error(error, error_size, "GeoMet city identifier is invalid");
        return -1;
    }

    for (size_t index = 3; index < identifier_length; index++) {
        unsigned char value = (unsigned char)identifier[index];
        if (!(islower(value) || isdigit(value) || value == '-')) {
            set_error(error, error_size, "GeoMet city identifier is invalid");
            return -1;
        }
    }

    const char *remainder = identifier + identifier_length;
    const char *end = input + length;
    if (remainder < end && *remainder == '/') remainder++;
    if (remainder < end && *remainder == '#') {
        set_error(error, error_size, "GeoMet URL cannot contain a fragment");
        return -1;
    }
    if (remainder < end && *remainder != '?') {
        set_error(error, error_size, "GeoMet URL path is invalid");
        return -1;
    }
    if (remainder < end && memchr(remainder, '#', (size_t)(end - remainder)) != NULL) {
        set_error(error, error_size, "GeoMet URL cannot contain a fragment");
        return -1;
    }

    int written = snprintf(
        canonical,
        canonical_size,
        "%s%.*s%s",
        MP_WEATHER_SOURCE_PREFIX,
        (int)identifier_length,
        identifier,
        MP_WEATHER_SOURCE_SUFFIX
    );
    if (written < 0 || (size_t)written >= canonical_size) {
        canonical[0] = '\0';
        set_error(error, error_size, "GeoMet URL is too long");
        return -1;
    }

    return 0;
}


int mp_weather_source_read(
    char *url,
    size_t url_size,
    char *error,
    size_t error_size
) {
    if (!url || url_size == 0) {
        set_error(error, error_size, "weather source output buffer is invalid");
        return -1;
    }

    FILE *file = fopen(MP_WEATHER_SOURCE_FILE, "r");
    if (!file) {
        if (errno == ENOENT) {
            snprintf(url, url_size, "%s", MP_WEATHER_SOURCE_DEFAULT);
            return 0;
        }
        char message[192];
        snprintf(message, sizeof(message), "cannot read weather source: %s", strerror(errno));
        set_error(error, error_size, message);
        return -1;
    }

    char input[MP_WEATHER_SOURCE_URL_MAX + 8];
    if (!fgets(input, sizeof(input), file)) {
        int saved_errno = ferror(file) ? errno : EINVAL;
        fclose(file);
        errno = saved_errno;
        set_error(error, error_size, "weather source file is empty or unreadable");
        return -1;
    }
    fclose(file);

    return mp_weather_source_validate(input, url, url_size, error, error_size);
}

int mp_weather_source_write(
    const char *input,
    char *canonical,
    size_t canonical_size,
    char *error,
    size_t error_size
) {
    if (mp_weather_source_validate(
            input, canonical, canonical_size, error, error_size) != 0) {
        return -1;
    }

    char temporary[] =
        "/var/lib/mk-piclock-weather/.weather-source.url.XXXXXX";
    int fd = mkstemp(temporary);
    if (fd < 0) {
        char message[192];
        snprintf(message, sizeof(message), "cannot create weather source: %s", strerror(errno));
        set_error(error, error_size, message);
        return -1;
    }

    size_t length = strlen(canonical);
    int result = 0;
    if (mp_write_full(fd, canonical, length) != 0 || mp_write_full(fd, "\n", 1) != 0 ||
        fchmod(fd, 0644) != 0 || fsync(fd) != 0) {
        result = -1;
    }

    int saved_errno = errno;
    if (close(fd) != 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }

    if (result == 0 && rename(temporary, MP_WEATHER_SOURCE_FILE) != 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result == 0 && mp_fsync_parent_directory(MP_WEATHER_SOURCE_FILE) != 0) {
        result = -1;
        saved_errno = errno;
    }

    if (result != 0) {
        unlink(temporary);
        char message[192];
        snprintf(message, sizeof(message), "cannot save weather source: %s", strerror(saved_errno));
        set_error(error, error_size, message);
        errno = saved_errno;
        return -1;
    }

    return 0;
}


static int read_json_file(
    const char *path,
    const char *fallback,
    char *json,
    size_t json_size,
    char *error,
    size_t error_size
) {
    if (!path || !json || json_size < 3) {
        set_error(error, error_size, "weather JSON output buffer is invalid");
        return -1;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT && fallback) {
            int written = snprintf(json, json_size, "%s", fallback);
            if (written < 0 || (size_t)written >= json_size) {
                set_error(error, error_size, "weather JSON fallback is too large");
                return -1;
            }
            return 0;
        }
        char message[192];
        snprintf(message, sizeof(message), "cannot read %s: %s", path, strerror(errno));
        set_error(error, error_size, message);
        return -1;
    }

    size_t used = fread(json, 1, json_size - 1, file);
    if (ferror(file)) {
        int saved_errno = errno;
        fclose(file);
        char message[192];
        snprintf(message, sizeof(message), "cannot read %s: %s", path, strerror(saved_errno));
        set_error(error, error_size, message);
        return -1;
    }
    if (!feof(file)) {
        fclose(file);
        set_error(error, error_size, "weather activity data is too large");
        return -1;
    }
    fclose(file);
    json[used] = '\0';

    while (used > 0 && isspace((unsigned char)json[used - 1])) {
        json[--used] = '\0';
    }
    size_t start = 0;
    while (start < used && isspace((unsigned char)json[start])) start++;
    if (start >= used || json[start] != '{' || json[used - 1] != '}') {
        set_error(error, error_size, "weather activity data is invalid");
        return -1;
    }
    if (start > 0) memmove(json, json + start, used - start + 1);
    return 0;
}

int mp_weather_status_read_json(
    char *json,
    size_t json_size,
    char *error,
    size_t error_size
) {
    return read_json_file(
        MP_WEATHER_STATUS_FILE,
        "{\"schema_version\":1,\"ok\":false,\"result\":\"pending\",\"message\":\"No weather update has run yet.\"}",
        json,
        json_size,
        error,
        error_size
    );
}

int mp_weather_activity_read_json(
    char *json,
    size_t json_size,
    char *error,
    size_t error_size
) {
    return read_json_file(
        MP_WEATHER_ACTIVITY_FILE,
        "{\"schema_version\":1,\"entries\":[]}",
        json,
        json_size,
        error,
        error_size
    );
}
