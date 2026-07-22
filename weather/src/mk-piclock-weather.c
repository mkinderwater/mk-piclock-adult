#define _GNU_SOURCE
#include <curl/curl.h>
#include <json-c/json.h>

#include "weather_frames.h"
#include "io_helpers.h"
#include "compiler_attrs.h"
#include "ipc_protocol.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define WEATHER_VERSION "2.0.10"
#define DEFAULT_USER_AGENT "mk-piclock-weather/" WEATHER_VERSION
#define DEFAULT_LOCATION_ID "ab-52"
#define DEFAULT_SOURCE_CONFIG "/var/lib/mk-piclock-weather/weather-source.url"
#define DEFAULT_FRAME_CONFIG MP_WEATHER_FRAMES_FILE
#define DEFAULT_CLOCK_API_URL "http://127.0.0.1:8080/api/v1/weather"
#define DEFAULT_TIMEZONE "America/Edmonton"
#define DEFAULT_STATUS_PATH "/var/lib/mk-piclock-weather/status.json"
#define DEFAULT_ACTIVITY_PATH "/var/lib/mk-piclock-weather/activity.json"
#define DEFAULT_OUTPUT_PATH "/run/mk-piclock/weather.json"
#define DEFAULT_CACHE_PATH "/var/lib/mk-piclock-weather/weather-last-good.json"
#define DEFAULT_ICON_LIBRARY_DIR "/usr/local/share/mk-piclock-weather/icons"
#define DEFAULT_ICON_RUNTIME_DIR "/run/mk-piclock/weather-icons"
#define DEFAULT_BASE_URL_PREFIX "https://api.weather.gc.ca/collections/citypageweather-realtime/items/"
#define MAX_RESPONSE_BYTES (4U * 1024U * 1024U)
#define MAX_API_RESPONSE_BYTES 65536U
#define WEATHER_ICON_RAW_BYTES 512U
#define UNKNOWN_ICON_CODE 29
#define ACTIVITY_LIMIT 50

struct error_info {
    char text[512];
};

struct memory_buffer {
    unsigned char *data;
    size_t size;
    size_t limit;
    bool overflow;
};


struct weather_slot {
    enum mp_weather_slot_kind kind;
    char label[8];
    char date_label[16];
    int temperature_c;
    bool temperature_available;
    int precipitation_probability_percent;
    int icon_code;
    char icon[16];
    time_t timestamp;
};

struct icon_result {
    int count;
    int codes[3];
    int substitution_count;
    char substitutions[3][128];
};

static void set_error(struct error_info *error, const char *format, ...) MP_PRINTF_LIKE(2, 3);

static void set_error(struct error_info *error, const char *format, ...)
{
    if (!error) return;
    va_list ap;
    va_start(ap, format);
    vsnprintf(error->text, sizeof(error->text), format, ap);
    va_end(ap);
}

static const char *env_value(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return (value && *value) ? value : fallback;
}

static bool env_int(const char *name, int fallback, int minimum, int maximum,
                    int *result, struct error_info *error)
{
    const char *raw = getenv(name);
    if (!raw || !*raw) {
        *result = fallback;
        return true;
    }
    char *end = NULL;
    errno = 0;
    long value = strtol(raw, &end, 10);
    if (errno || end == raw || *end || value < minimum || value > maximum) {
        set_error(error, "%s must be an integer in %d..%d", name, minimum, maximum);
        return false;
    }
    *result = (int)value;
    return true;
}

static char *trim(char *text)
{
    while (*text && isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return text;
}

static bool mkdir_p(const char *path, mode_t mode, struct error_info *error)
{
    if (!path || !*path) return true;
    char copy[PATH_MAX];
    if (snprintf(copy, sizeof(copy), "%s", path) >= (int)sizeof(copy)) {
        set_error(error, "path is too long: %s", path);
        return false;
    }
    size_t len = strlen(copy);
    if (len > 1 && copy[len - 1] == '/') copy[len - 1] = '\0';
    for (char *p = copy + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(copy, mode) < 0 && errno != EEXIST) {
            set_error(error, "cannot create directory %s: %s", copy, strerror(errno));
            return false;
        }
        *p = '/';
    }
    if (mkdir(copy, mode) < 0 && errno != EEXIST) {
        set_error(error, "cannot create directory %s: %s", copy, strerror(errno));
        return false;
    }
    return true;
}

static bool ensure_parent_directory(const char *path, struct error_info *error)
{
    char copy[PATH_MAX];
    if (snprintf(copy, sizeof(copy), "%s", path) >= (int)sizeof(copy)) {
        set_error(error, "path is too long: %s", path);
        return false;
    }
    char *slash = strrchr(copy, '/');
    if (!slash || slash == copy) return true;
    *slash = '\0';
    return mkdir_p(copy, 0755, error);
}

static bool atomic_write_bytes(const char *path, const void *data, size_t length,
                               mode_t mode, struct error_info *error)
{
    if (!ensure_parent_directory(path, error)) return false;
    char temp[PATH_MAX];
    if (snprintf(temp, sizeof(temp), "%s.XXXXXX", path) >= (int)sizeof(temp)) {
        set_error(error, "temporary path is too long: %s", path);
        return false;
    }
    int fd = mkstemp(temp);
    if (fd < 0) {
        set_error(error, "cannot create temporary file for %s: %s", path, strerror(errno));
        return false;
    }
    bool ok = true;
    const unsigned char *cursor = data;
    size_t remaining = length;
    while (remaining > 0) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            set_error(error, "cannot write %s: %s", path, strerror(errno));
            ok = false;
            break;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    if (ok && fsync(fd) < 0) {
        set_error(error, "cannot sync %s: %s", path, strerror(errno));
        ok = false;
    }
    if (ok && fchmod(fd, mode) < 0) {
        set_error(error, "cannot set permissions on %s: %s", path, strerror(errno));
        ok = false;
    }
    if (close(fd) < 0 && ok) {
        set_error(error, "cannot close %s: %s", path, strerror(errno));
        ok = false;
    }
    if (ok && rename(temp, path) < 0) {
        set_error(error, "cannot replace %s: %s", path, strerror(errno));
        ok = false;
    }
    if (!ok) unlink(temp);
    else (void)mp_fsync_parent_directory(path);
    return ok;
}

static bool atomic_write_json(const char *path, json_object *object,
                              struct error_info *error)
{
    const char *serialized = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    size_t length = strlen(serialized);
    char *with_newline = malloc(length + 2);
    if (!with_newline) {
        set_error(error, "out of memory while writing %s", path);
        return false;
    }
    memcpy(with_newline, serialized, length);
    with_newline[length] = '\n';
    with_newline[length + 1] = '\0';
    bool ok = atomic_write_bytes(path, with_newline, length + 1, 0644, error);
    free(with_newline);
    return ok;
}

static bool read_file(const char *path, unsigned char **data_out, size_t *length_out,
                      size_t limit, struct error_info *error, int *error_number)
{
    *data_out = NULL;
    *length_out = 0;
    if (error_number) *error_number = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        int saved_errno = errno;
        set_error(error, "cannot read %s: %s", path, strerror(saved_errno));
        if (error_number) *error_number = saved_errno;
        return false;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        int saved_errno = errno;
        set_error(error, "cannot inspect %s: %s", path, strerror(saved_errno));
        close(fd);
        if (error_number) *error_number = saved_errno;
        return false;
    }
    if (st.st_size < 0 || (uintmax_t)st.st_size > limit) {
        set_error(error, "%s exceeds its size limit", path);
        close(fd);
        if (error_number) *error_number = EFBIG;
        return false;
    }
    size_t capacity = (size_t)st.st_size + 1;
    unsigned char *data = malloc(capacity ? capacity : 1);
    if (!data) {
        set_error(error, "out of memory while reading %s", path);
        close(fd);
        if (error_number) *error_number = ENOMEM;
        return false;
    }
    size_t used = 0;
    while (used < (size_t)st.st_size) {
        ssize_t count = read(fd, data + used, (size_t)st.st_size - used);
        if (count < 0) {
            if (errno == EINTR) continue;
            int saved_errno = errno;
            set_error(error, "cannot read %s: %s", path, strerror(saved_errno));
            free(data);
            close(fd);
            if (error_number) *error_number = saved_errno;
            return false;
        }
        if (count == 0) break;
        used += (size_t)count;
    }
    close(fd);
    data[used] = '\0';
    *data_out = data;
    *length_out = used;
    return true;
}

static json_object *parse_json_document(const unsigned char *data, size_t length)
{
    if (!data || length > INT_MAX) return NULL;
    json_tokener *tokener = json_tokener_new();
    if (!tokener) return NULL;
    json_object *object = json_tokener_parse_ex(tokener, (const char *)data, (int)length);
    enum json_tokener_error parse_error = json_tokener_get_error(tokener);
    size_t parsed = json_tokener_get_parse_end(tokener);
    while (parsed < length && isspace(data[parsed])) parsed++;
    json_tokener_free(tokener);
    if (parse_error != json_tokener_success || parsed != length) {
        if (object) json_object_put(object);
        return NULL;
    }
    return object;
}

static json_object *read_json_file(const char *path)
{
    unsigned char *data = NULL;
    size_t length = 0;
    struct error_info ignored = {{0}};
    if (!read_file(path, &data, &length, MAX_RESPONSE_BYTES, &ignored, NULL)) return NULL;
    json_object *object = parse_json_document(data, length);
    free(data);
    return object;
}

static json_object *jget(json_object *object, const char *key)
{
    json_object *value = NULL;
    if (!object || !json_object_is_type(object, json_type_object)) return NULL;
    return json_object_object_get_ex(object, key, &value) ? value : NULL;
}

static json_object *jdeep2(json_object *object, const char *a, const char *b)
{
    return jget(jget(object, a), b);
}

static json_object *jdeep3(json_object *object, const char *a, const char *b, const char *c)
{
    return jget(jget(jget(object, a), b), c);
}

static json_object *language_value(json_object *value)
{
    if (!value || !json_object_is_type(value, json_type_object)) return value;
    json_object *nested = jget(value, "en");
    if (nested) return language_value(nested);
    nested = jget(value, "value");
    if (nested) return language_value(nested);
    nested = jget(value, "fr");
    if (nested) return language_value(nested);
    return NULL;
}

static bool as_text(json_object *value, char *output, size_t output_size)
{
    value = language_value(value);
    if (!value || !output || output_size == 0) return false;
    const char *text = NULL;
    char scratch[64];
    switch (json_object_get_type(value)) {
        case json_type_string:
            text = json_object_get_string(value);
            break;
        case json_type_int:
            snprintf(scratch, sizeof(scratch), "%lld", (long long)json_object_get_int64(value));
            text = scratch;
            break;
        case json_type_double:
            snprintf(scratch, sizeof(scratch), "%.15g", json_object_get_double(value));
            text = scratch;
            break;
        case json_type_boolean:
            text = json_object_get_boolean(value) ? "true" : "false";
            break;
        default:
            return false;
    }
    if (!text) return false;
    while (*text && isspace((unsigned char)*text)) text++;
    if (!*text) return false;
    snprintf(output, output_size, "%s", text);
    char *clean = trim(output);
    if (clean != output) memmove(output, clean, strlen(clean) + 1);
    return *output != '\0';
}

static bool as_number(json_object *value, double *output)
{
    value = language_value(value);
    if (!value || !output || json_object_is_type(value, json_type_boolean)) return false;
    if (json_object_is_type(value, json_type_int) || json_object_is_type(value, json_type_double)) {
        double number = json_object_get_double(value);
        if (!isfinite(number)) return false;
        *output = number;
        return true;
    }
    if (json_object_is_type(value, json_type_string)) {
        const char *text = json_object_get_string(value);
        if (!text) return false;
        while (*text && !isdigit((unsigned char)*text) && *text != '+' && *text != '-' && *text != '.') text++;
        if (!*text) return false;
        char *end = NULL;
        errno = 0;
        double number = strtod(text, &end);
        if (errno || end == text || !isfinite(number)) return false;
        *output = number;
        return true;
    }
    return false;
}

static bool as_int(json_object *value, int *output)
{
    double number = 0.0;
    if (!as_number(value, &number)) return false;
    if (number < INT_MIN || number > INT_MAX) return false;
    *output = (int)number;
    return true;
}

static void add_text_if(json_object *target, const char *key, json_object *value)
{
    char text[512];
    if (as_text(value, text, sizeof(text))) json_object_object_add(target, key, json_object_new_string(text));
}

static void add_number_if(json_object *target, const char *key, json_object *value)
{
    double number = 0.0;
    if (!as_number(value, &number)) return;
    double rounded = round(number);
    if (fabs(number - rounded) < 1e-9) json_object_object_add(target, key, json_object_new_int64((int64_t)rounded));
    else json_object_object_add(target, key, json_object_new_double(number));
}

static void add_int_if(json_object *target, const char *key, json_object *value)
{
    int number = 0;
    if (as_int(value, &number)) json_object_object_add(target, key, json_object_new_int(number));
}

static bool parse_iso8601(const char *text, time_t *result)
{
    if (!text || !*text || !result) return false;
    int year, month, day, hour, minute, second;
    if (sscanf(text, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) return false;
    struct tm tm_value = {0};
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = day;
    tm_value.tm_hour = hour;
    tm_value.tm_min = minute;
    tm_value.tm_sec = second;
    tm_value.tm_isdst = 0;
    time_t value = timegm(&tm_value);
    if (value == (time_t)-1) return false;
    const char *suffix = strchr(text, 'T');
    if (!suffix) return false;
    suffix += 1;
    for (int colons = 0; *suffix; suffix++) {
        if (*suffix == ':') colons++;
        if (colons >= 2) {
            suffix++;
            while (isdigit((unsigned char)*suffix)) suffix++;
            if (*suffix == '.') {
                suffix++;
                while (isdigit((unsigned char)*suffix)) suffix++;
            }
            break;
        }
    }
    if (*suffix == 'Z' || *suffix == '\0') {
        *result = value;
        return true;
    }
    if (*suffix != '+' && *suffix != '-') return false;
    int sign = (*suffix == '+') ? 1 : -1;
    int offset_hour = 0, offset_minute = 0;
    if (sscanf(suffix + 1, "%d:%d", &offset_hour, &offset_minute) < 1) return false;
    value -= sign * (offset_hour * 3600 + offset_minute * 60);
    *result = value;
    return true;
}

static void iso_z(time_t value, char output[32])
{
    struct tm tm_value;
    gmtime_r(&value, &tm_value);
    strftime(output, 32, "%Y-%m-%dT%H:%M:%SZ", &tm_value);
}

static bool validate_source_url(const char *input, char *canonical, size_t canonical_size,
                                char *location_id, size_t id_size, struct error_info *error)
{
    if (!input) {
        set_error(error, "weather source URL is empty");
        return false;
    }
    char copy[4096];
    if (snprintf(copy, sizeof(copy), "%s", input) >= (int)sizeof(copy)) {
        set_error(error, "weather source URL is too long");
        return false;
    }
    char *url = trim(copy);
    size_t prefix_length = strlen(DEFAULT_BASE_URL_PREFIX);
    if (strncmp(url, DEFAULT_BASE_URL_PREFIX, prefix_length) != 0) {
        set_error(error, "weather source URL must use the ECCC citypageweather-realtime HTTPS endpoint");
        return false;
    }
    const char *id_start = url + prefix_length;
    const char *id_end = id_start;
    while (*id_end && *id_end != '?' && *id_end != '/') id_end++;
    size_t id_length = (size_t)(id_end - id_start);
    if (id_length < 4 || id_length >= id_size || id_start[2] != '-') {
        set_error(error, "weather source URL has an invalid location ID");
        return false;
    }
    for (size_t i = 0; i < id_length; i++) {
        unsigned char ch = (unsigned char)id_start[i];
        if (i == 2) continue;
        if (!(islower(ch) || isdigit(ch) || ch == '-')) {
            set_error(error, "weather source URL has an invalid location ID");
            return false;
        }
    }
    if (*id_end == '/') id_end++;
    if (*id_end && strcmp(id_end, "?f=json") != 0) {
        set_error(error, "weather source URL must point to one ECCC city item");
        return false;
    }
    memcpy(location_id, id_start, id_length);
    location_id[id_length] = '\0';
    if (snprintf(canonical, canonical_size, "%s%s?f=json", DEFAULT_BASE_URL_PREFIX, location_id) >= (int)canonical_size) {
        set_error(error, "canonical weather source URL is too long");
        return false;
    }
    return true;
}

static bool resolve_source(const char *source_path, char *source_url, size_t source_url_size,
                           char *location_id, size_t id_size, struct error_info *error)
{
    unsigned char *data = NULL;
    size_t length = 0;
    struct error_info read_error = {{0}};
    int read_errno = 0;
    if (read_file(source_path, &data, &length, 4096, &read_error, &read_errno)) {
        char *line = (char *)data;
        char *newline = strpbrk(line, "\r\n");
        if (newline) *newline = '\0';
        char *clean = trim(line);
        if (*clean) {
            bool ok = validate_source_url(clean, source_url, source_url_size, location_id, id_size, error);
            free(data);
            return ok;
        }
        free(data);
    } else if (read_errno != ENOENT) {
        set_error(error, "%s", read_error.text);
        return false;
    }
    char fallback[256];
    snprintf(fallback, sizeof(fallback), "%s%s?f=json", DEFAULT_BASE_URL_PREFIX, DEFAULT_LOCATION_ID);
    return validate_source_url(fallback, source_url, source_url_size, location_id, id_size, error);
}

static size_t weather_curl_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct memory_buffer *buffer = userdata;
    if (nmemb && size > SIZE_MAX / nmemb) return 0;
    size_t amount = size * nmemb;
    if (buffer->size > buffer->limit || amount > buffer->limit - buffer->size) {
        buffer->overflow = true;
        return 0;
    }
    unsigned char *resized = realloc(buffer->data, buffer->size + amount + 1);
    if (!resized) return 0;
    buffer->data = resized;
    memcpy(buffer->data + buffer->size, ptr, amount);
    buffer->size += amount;
    buffer->data[buffer->size] = '\0';
    return amount;
}

static bool curl_request(const char *url, const char *post_fields, long timeout_seconds,
                         const char *user_agent, size_t response_limit,
                         const char *protocols, struct memory_buffer *response,
                         long *http_status, char **content_type, struct error_info *error)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        set_error(error, "cannot initialize libcurl");
        return false;
    }
    response->data = NULL;
    response->size = 0;
    response->limit = response_limit;
    response->overflow = false;
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, post_fields ? "Accept: application/json" : "Accept: application/geo+json, application/json;q=0.9");
    if (post_fields) headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, weather_curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, protocols);
    if (post_fields) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(post_fields));
    }
    CURLcode code = curl_easy_perform(curl);
    if (http_status) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_status);
    char *type = NULL;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type);
    if (content_type) *content_type = type ? strdup(type) : NULL;

    bool ok = true;
    if (code != CURLE_OK) {
        if (response->overflow) set_error(error, "HTTP response exceeds size limit");
        else set_error(error, "HTTP request failed: %s", curl_easy_strerror(code));
        ok = false;
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok;
}

static bool content_type_is_json(const char *content_type)
{
    if (!content_type) return false;
    return strncasecmp(content_type, "application/json", 16) == 0 ||
           strncasecmp(content_type, "application/geo+json", 20) == 0 ||
           strncasecmp(content_type, "application/ld+json", 19) == 0 ||
           strncasecmp(content_type, "text/json", 9) == 0;
}

static json_object *fetch_json(const char *url, int timeout_seconds, const char *user_agent,
                               struct error_info *error)
{
    struct memory_buffer response = {0};
    long status = 0;
    char *content_type = NULL;
    if (!curl_request(url, NULL, timeout_seconds, user_agent, MAX_RESPONSE_BYTES,
                      "https", &response, &status, &content_type, error)) {
        free(response.data);
        free(content_type);
        return NULL;
    }
    if (status < 200 || status >= 300) {
        set_error(error, "GeoMet HTTP error %ld", status);
        free(response.data);
        free(content_type);
        return NULL;
    }
    if (!content_type_is_json(content_type)) {
        set_error(error, "unexpected GeoMet content type: %s", content_type ? content_type : "missing");
        free(response.data);
        free(content_type);
        return NULL;
    }
    free(content_type);
    json_object *object = parse_json_document(response.data, response.size);
    free(response.data);
    if (!object || !json_object_is_type(object, json_type_object)) {
        if (object) json_object_put(object);
        set_error(error, "GeoMet returned invalid JSON");
        return NULL;
    }
    return object;
}

static json_object *normalize_forecast(json_object *item)
{
    json_object *output = json_object_new_object();
    json_object *period = jget(item, "period");
    json_object *name = jget(period, "textForecastName");
    if (!name) name = jget(period, "value");
    if (!name) name = jget(item, "textForecast_name");
    add_text_if(output, "period", name);

    json_object *temperature = jdeep2(item, "temperatures", "temperature");
    json_object *temp_value = jget(temperature, "value");
    json_object *temp_class = jget(temperature, "class");
    if (!temp_value) {
        temp_value = jdeep2(item, "temperatures", "temp_high");
        if (temp_value && !temp_class) json_object_object_add(output, "temperature_type", json_object_new_string("high"));
    }
    if (!temp_value) {
        temp_value = jdeep2(item, "temperatures", "temp_low");
        if (temp_value && !temp_class) json_object_object_add(output, "temperature_type", json_object_new_string("low"));
    }
    add_number_if(output, "temperature_c", temp_value);
    char class_text[64];
    if (as_text(temp_class, class_text, sizeof(class_text))) {
        for (char *p = class_text; *p; p++) *p = (char)tolower((unsigned char)*p);
        json_object_object_add(output, "temperature_type", json_object_new_string(class_text));
    }

    json_object *abbreviated = jget(item, "abbreviatedForecast");
    json_object *legacy = jget(item, "abbreviated_forecast");
    json_object *icon = jget(abbreviated, "icon");
    if (!icon) icon = jget(legacy, "icon");
    json_object *condition = jget(abbreviated, "textSummary");
    if (!condition) condition = jget(legacy, "text_summary");
    if (!condition) condition = jget(item, "cloudPrecip");
    if (!condition) condition = jget(item, "cloud_precip");
    add_text_if(output, "condition", condition);
    json_object *icon_value = jget(icon, "value");
    add_int_if(output, "icon_code", icon_value ? icon_value : icon);
    add_text_if(output, "icon_url", jget(icon, "url"));
    add_int_if(output, "precip_probability_percent", jget(legacy, "pop"));
    json_object *wind = jdeep2(item, "winds", "textSummary");
    if (!wind) wind = jdeep2(item, "winds", "text_summary");
    add_text_if(output, "wind", wind);
    add_text_if(output, "summary", jget(item, "textSummary"));
    return output;
}

static json_object *normalize_hourly(json_object *item)
{
    json_object *output = json_object_new_object();
    add_text_if(output, "time", jget(item, "timestamp"));
    add_number_if(output, "temperature_c", jdeep2(item, "temperature", "value"));
    add_text_if(output, "condition", jget(item, "condition"));
    add_int_if(output, "icon_code", jdeep2(item, "iconCode", "value"));
    add_text_if(output, "icon_url", jdeep2(item, "iconCode", "url"));
    add_int_if(output, "precip_probability_percent", jdeep2(item, "lop", "value"));
    add_number_if(output, "wind_kmh", jdeep3(item, "wind", "speed", "value"));
    add_number_if(output, "wind_gust_kmh", jdeep3(item, "wind", "gust", "value"));
    json_object *direction = jdeep2(item, "wind", "direction");
    json_object *direction_value = jget(direction, "value");
    add_text_if(output, "wind_direction", direction_value ? direction_value : direction);
    add_number_if(output, "humidex_c", jdeep2(item, "humidex", "value"));
    add_number_if(output, "wind_chill_c", jdeep2(item, "windChill", "value"));
    add_number_if(output, "uv_index", jdeep3(item, "uv", "index", "value"));
    return output;
}

static json_object *normalize_warning(json_object *item)
{
    json_object *output = json_object_new_object();
    add_text_if(output, "type", jget(item, "type"));
    add_text_if(output, "description", jget(item, "description"));
    add_text_if(output, "priority", jget(item, "priority"));
    add_text_if(output, "colour", jget(item, "alertColourLevel"));
    add_text_if(output, "issued_at", jget(item, "eventIssue"));
    add_text_if(output, "expires_at", jget(item, "expiryTime"));
    add_text_if(output, "url", jget(item, "url"));
    return output;
}

static json_object *as_array_or_single(json_object *value)
{
    if (!value) return NULL;
    if (json_object_is_type(value, json_type_array)) return value;
    if (json_object_is_type(value, json_type_object)) return value;
    return NULL;
}

static json_object *normalize_weather(json_object *raw, const char *expected_location_id,
                                      const char *source_url, int forecast_limit,
                                      int hourly_limit, int stale_after_seconds,
                                      int expire_after_seconds, time_t fetched_at,
                                      struct error_info *error)
{
    if (!raw || !json_object_is_type(raw, json_type_object)) {
        set_error(error, "GeoMet response is not a JSON object");
        return NULL;
    }
    json_object *properties = jget(raw, "properties");
    json_object *root = properties && json_object_is_type(properties, json_type_object) ? properties : raw;
    char identifier[128];
    if (!as_text(jget(root, "identifier"), identifier, sizeof(identifier)) &&
        !as_text(jget(raw, "id"), identifier, sizeof(identifier))) {
        set_error(error, "GeoMet response has no identifier");
        return NULL;
    }
    if (strcmp(identifier, expected_location_id) != 0) {
        set_error(error, "GeoMet identifier mismatch: expected %s, got %s", expected_location_id, identifier);
        return NULL;
    }

    json_object *output = json_object_new_object();
    json_object_object_add(output, "schema_version", json_object_new_int(1));
    json_object_object_add(output, "service_version", json_object_new_string(WEATHER_VERSION));
    json_object_object_add(output, "source", json_object_new_string("Environment and Climate Change Canada GeoMet"));
    json_object_object_add(output, "source_url", json_object_new_string(source_url));
    json_object_object_add(output, "location_id", json_object_new_string(identifier));
    char text[512];
    if (as_text(jget(root, "name"), text, sizeof(text))) json_object_object_add(output, "location", json_object_new_string(text));
    else json_object_object_add(output, "location", json_object_new_string(expected_location_id));
    add_text_if(output, "region", jget(root, "region"));
    char fetched_text[32];
    iso_z(fetched_at, fetched_text);
    json_object_object_add(output, "fetched_at", json_object_new_string(fetched_text));

    char updated_text[128] = "";
    time_t updated_at = 0;
    bool have_updated = as_text(jget(root, "lastUpdated"), updated_text, sizeof(updated_text));
    if (have_updated) json_object_object_add(output, "source_updated_at", json_object_new_string(updated_text));
    else json_object_object_add(output, "source_updated_at", NULL);
    int64_t age = 0;
    bool have_age = have_updated && parse_iso8601(updated_text, &updated_at);
    if (have_age) {
        age = (int64_t)difftime(fetched_at, updated_at);
        if (age < 0) age = 0;
        json_object_object_add(output, "source_age_seconds", json_object_new_int64(age));
    } else {
        json_object_object_add(output, "source_age_seconds", NULL);
    }
    json_object_object_add(output, "stale_after_seconds", json_object_new_int(stale_after_seconds));
    json_object_object_add(output, "expire_after_seconds", json_object_new_int(expire_after_seconds));
    json_object_object_add(output, "stale", json_object_new_boolean(have_age && age >= stale_after_seconds));
    json_object_object_add(output, "expired", json_object_new_boolean(have_age && age >= expire_after_seconds));

    json_object *current = jget(root, "currentConditions");
    json_object *current_out = json_object_new_object();
    add_text_if(current_out, "observed_at", jget(current, "timestamp"));
    add_number_if(current_out, "temperature_c", jdeep2(current, "temperature", "value"));
    add_text_if(current_out, "condition", jget(current, "condition"));
    add_int_if(current_out, "icon_code", jdeep2(current, "iconCode", "value"));
    add_text_if(current_out, "icon_url", jdeep2(current, "iconCode", "url"));
    add_number_if(current_out, "humidity_percent", jdeep2(current, "relativeHumidity", "value"));
    add_number_if(current_out, "dewpoint_c", jdeep2(current, "dewpoint", "value"));
    add_number_if(current_out, "pressure_kpa", jdeep2(current, "pressure", "value"));
    add_text_if(current_out, "pressure_tendency", jdeep2(current, "pressure", "tendency"));
    add_number_if(current_out, "wind_kmh", jdeep3(current, "wind", "speed", "value"));
    add_number_if(current_out, "wind_gust_kmh", jdeep3(current, "wind", "gust", "value"));
    json_object *wind_direction = jdeep2(current, "wind", "direction");
    json_object *wind_direction_value = jget(wind_direction, "value");
    add_text_if(current_out, "wind_direction", wind_direction_value ? wind_direction_value : wind_direction);
    add_number_if(current_out, "wind_bearing_deg", jdeep3(current, "wind", "bearing", "value"));
    add_number_if(current_out, "wind_chill_c", jdeep2(current, "windChill", "value"));
    add_number_if(current_out, "humidex_c", jdeep2(current, "humidex", "value"));
    add_text_if(current_out, "station", jdeep2(current, "station", "value"));
    add_text_if(current_out, "station_code", jdeep2(current, "station", "code"));
    json_object_object_add(output, "current", current_out);

    json_object *forecast_out = json_object_new_array();
    json_object *forecast_items = as_array_or_single(jdeep2(root, "forecastGroup", "forecasts"));
    if (forecast_items && json_object_is_type(forecast_items, json_type_array)) {
        int count = json_object_array_length(forecast_items);
        if (count > forecast_limit) count = forecast_limit;
        for (int i = 0; i < count; i++) {
            json_object *item = json_object_array_get_idx(forecast_items, i);
            if (json_object_is_type(item, json_type_object)) json_object_array_add(forecast_out, normalize_forecast(item));
        }
    } else if (forecast_items && forecast_limit > 0) {
        json_object_array_add(forecast_out, normalize_forecast(forecast_items));
    }
    json_object_object_add(output, "forecast", forecast_out);

    json_object *hourly_out = json_object_new_array();
    json_object *hourly_items = as_array_or_single(jdeep2(root, "hourlyForecastGroup", "hourlyForecasts"));
    if (hourly_items && json_object_is_type(hourly_items, json_type_array)) {
        int count = json_object_array_length(hourly_items);
        if (count > hourly_limit) count = hourly_limit;
        for (int i = 0; i < count; i++) {
            json_object *item = json_object_array_get_idx(hourly_items, i);
            if (json_object_is_type(item, json_type_object)) json_object_array_add(hourly_out, normalize_hourly(item));
        }
    } else if (hourly_items && hourly_limit > 0) {
        json_object_array_add(hourly_out, normalize_hourly(hourly_items));
    }
    json_object_object_add(output, "hourly", hourly_out);

    json_object *sun = json_object_new_object();
    add_text_if(sun, "sunrise", jdeep2(root, "riseSet", "sunrise"));
    add_text_if(sun, "sunset", jdeep2(root, "riseSet", "sunset"));
    json_object_object_add(output, "sun", sun);

    json_object *warnings_out = json_object_new_array();
    json_object *warnings = as_array_or_single(jget(root, "warnings"));
    if (warnings && json_object_is_type(warnings, json_type_array)) {
        int count = json_object_array_length(warnings);
        for (int i = 0; i < count; i++) {
            json_object *item = json_object_array_get_idx(warnings, i);
            if (json_object_is_type(item, json_type_object)) json_object_array_add(warnings_out, normalize_warning(item));
        }
    } else if (warnings) {
        json_object_array_add(warnings_out, normalize_warning(warnings));
    }
    json_object_object_add(output, "warnings", warnings_out);
    return output;
}

static bool timezone_is_valid(const char *name)
{
    if (!name || !*name || name[0] == '/' || strstr(name, "..")) return false;
    if (strcmp(name, "UTC") == 0 || strcmp(name, "GMT") == 0) return true;
    for (const char *p = name; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (!(isalnum(ch) || ch == '/' || ch == '_' || ch == '-' || ch == '+')) return false;
    }
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "/usr/share/zoneinfo/%s", name) >= (int)sizeof(path)) return false;
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static time_t local_frame_target(time_t now, const struct mp_weather_frame_selection *selection)
{
    if (!selection || selection->mode != MP_WEATHER_FRAME_OFFSET) return now;
    return now + selection->offset_hours * 3600;
}

static void short_hour_label(time_t timestamp, const char *timezone_name, char output[8])
{
    const char *old_tz_env = getenv("TZ");
    char *old_tz = old_tz_env ? strdup(old_tz_env) : NULL;
    setenv("TZ", timezone_name, 1);
    tzset();
    struct tm local;
    localtime_r(&timestamp, &local);
    int hour = local.tm_hour % 12;
    if (hour == 0) hour = 12;
    snprintf(output, 8, "%d%s", hour, local.tm_hour < 12 ? "AM" : "PM");
    if (old_tz) {
        setenv("TZ", old_tz, 1);
        free(old_tz);
    } else unsetenv("TZ");
    tzset();
}

static void future_date_label(time_t timestamp, time_t now,
                              const char *timezone_name, char output[16])
{
    static const char *months[] = {
        "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
        "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"
    };
    output[0] = '\0';
    if (timestamp <= 0 || timestamp <= now) return;

    const char *old_tz_env = getenv("TZ");
    char *old_tz = old_tz_env ? strdup(old_tz_env) : NULL;
    if (setenv("TZ", timezone_name, 1) < 0) {
        free(old_tz);
        return;
    }
    tzset();

    struct tm target_local;
    struct tm now_local;
    localtime_r(&timestamp, &target_local);
    localtime_r(&now, &now_local);

    bool later_date = target_local.tm_year > now_local.tm_year ||
        (target_local.tm_year == now_local.tm_year && target_local.tm_yday > now_local.tm_yday);
    if (later_date) {
        int month = target_local.tm_mon >= 0 && target_local.tm_mon <= 11 ? target_local.tm_mon : 0;
        snprintf(output, 16, "%s %d", months[month], target_local.tm_mday);
    }

    if (old_tz) {
        setenv("TZ", old_tz, 1);
        free(old_tz);
    } else {
        unsetenv("TZ");
    }
    tzset();
}

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int rounded_json_int(json_object *object, int fallback)
{
    double number = 0.0;
    return as_number(object, &number) ? (int)lround(number) : fallback;
}

static const char *legacy_icon_name(int code)
{
    switch (code) {
        case 0: case 1: case 30: case 31: return "clear";
        case 2: case 3: case 4: case 5: case 22: case 32: case 33: case 34: case 35: return "partly";
        case 6: case 12: case 13: case 28: case 36: return "rain";
        case 7: case 8: case 15: case 16: case 17: case 18: case 25: case 26: case 27: case 37: case 38: case 40: return "snow";
        case 9: case 19: case 39: case 46: case 47: return "storm";
        case 23: case 24: case 44: return "fog";
        case 41: case 42: case 43: case 45: case 48: return "wind";
        default: return "cloudy";
    }
}

struct hourly_candidate {
    time_t timestamp;
    json_object *entry;
};

static int compare_candidates(const void *left, const void *right)
{
    const struct hourly_candidate *a = left;
    const struct hourly_candidate *b = right;
    return (a->timestamp > b->timestamp) - (a->timestamp < b->timestamp);
}

static bool make_slot_from_hourly(json_object *entry, time_t timestamp, time_t now,
                                  const char *timezone_name, struct weather_slot *slot)
{
    memset(slot, 0, sizeof(*slot));
    slot->kind = MP_WEATHER_SLOT_FORECAST;
    short_hour_label(timestamp, timezone_name, slot->label);
    future_date_label(timestamp, now, timezone_name, slot->date_label);
    double temperature = 0.0;
    if (as_number(jget(entry, "temperature_c"), &temperature)) {
        slot->temperature_c = clamp_int((int)lround(temperature), -99, 99);
        slot->temperature_available = true;
    }
    slot->precipitation_probability_percent = clamp_int(rounded_json_int(jget(entry, "precip_probability_percent"), 0), 0, 100);
    if (!as_int(jget(entry, "icon_code"), &slot->icon_code)) slot->icon_code = UNKNOWN_ICON_CODE;
    snprintf(slot->icon, sizeof(slot->icon), "%s", legacy_icon_name(slot->icon_code));
    slot->timestamp = timestamp;
    return true;
}

static void sanitized_forecast_label(json_object *value, char output[8])
{
    char raw[128];
    if (!as_text(value, raw, sizeof(raw))) snprintf(raw, sizeof(raw), "LATER");
    size_t used = 0;
    for (const char *p = raw; *p && used < 7; p++) {
        if (isalnum((unsigned char)*p)) output[used++] = (char)toupper((unsigned char)*p);
    }
    if (used == 0) memcpy(output, "LATER", 5), used = 5;
    output[used] = '\0';
}

static bool build_outside_slot(json_object *normalized, time_t now,
                               const struct hourly_candidate *candidates, int candidate_count,
                               struct weather_slot *outside)
{
    memset(outside, 0, sizeof(*outside));
    outside->kind = MP_WEATHER_SLOT_OUTSIDE;
    snprintf(outside->label, sizeof(outside->label), "OUTSIDE");
    outside->icon_code = UNKNOWN_ICON_CODE;
    outside->timestamp = now;

    json_object *current = jget(normalized, "current");
    double temperature = 0.0;
    bool have_temperature = as_number(jget(current, "temperature_c"), &temperature);
    if (have_temperature) {
        outside->temperature_c = clamp_int((int)lround(temperature), -99, 99);
        outside->temperature_available = true;
    }
    int icon_code = UNKNOWN_ICON_CODE;
    bool have_icon = as_int(jget(current, "icon_code"), &icon_code);
    if (have_icon) outside->icon_code = icon_code;

    if (!have_temperature) {
        for (int i = 0; i < candidate_count; i++) {
            if (!as_number(jget(candidates[i].entry, "temperature_c"), &temperature)) continue;
            outside->temperature_c = clamp_int((int)lround(temperature), -99, 99);
            outside->temperature_available = true;
            outside->timestamp = candidates[i].timestamp;
            break;
        }
    }
    if (!have_icon) {
        for (int i = 0; i < candidate_count; i++) {
            if (!as_int(jget(candidates[i].entry, "icon_code"), &icon_code)) continue;
            outside->icon_code = icon_code;
            break;
        }
    }
    for (int i = 0; i < candidate_count; i++) {
        json_object *pop = jget(candidates[i].entry, "precip_probability_percent");
        if (!pop) continue;
        outside->precipitation_probability_percent =
            clamp_int(rounded_json_int(pop, 0), 0, 100);
        break;
    }
    snprintf(outside->icon, sizeof(outside->icon), "%s",
             legacy_icon_name(outside->icon_code));
    return outside->temperature_available;
}

static bool select_clock_slots(json_object *normalized, time_t now,
                               const struct mp_weather_frames_config *frames,
                               const char *timezone_name,
                               struct weather_slot slots[3],
                               struct error_info *error)
{
    if (!timezone_is_valid(timezone_name)) {
        set_error(error, "unknown timezone: %s", timezone_name ? timezone_name : "");
        return false;
    }
    memset(slots, 0, sizeof(struct weather_slot) * 3);

    struct hourly_candidate candidates[64];
    int candidate_count = 0;
    json_object *hourly = jget(normalized, "hourly");
    if (hourly && json_object_is_type(hourly, json_type_array)) {
        int count = json_object_array_length(hourly);
        if (count > 64) count = 64;
        for (int i = 0; i < count; i++) {
            json_object *entry = json_object_array_get_idx(hourly, i);
            char timestamp_text[128];
            time_t timestamp;
            if (!as_text(jget(entry, "time"), timestamp_text, sizeof(timestamp_text)) ||
                !parse_iso8601(timestamp_text, &timestamp) || timestamp < now - 1800) continue;
            candidates[candidate_count++] = (struct hourly_candidate){timestamp, entry};
        }
    }
    qsort(candidates, (size_t)candidate_count, sizeof(candidates[0]), compare_candidates);

    struct weather_slot outside;
    build_outside_slot(normalized, now, candidates, candidate_count, &outside);

    json_object *forecasts = jget(normalized, "forecast");
    int forecast_count = forecasts && json_object_is_type(forecasts, json_type_array)
        ? json_object_array_length(forecasts) : 0;

    for (int output_index = 0; output_index < 3; output_index++) {
        const struct mp_weather_frame_selection *selection = &frames->slots[output_index];
        if (selection->mode == MP_WEATHER_FRAME_ROOM) {
            memset(&slots[output_index], 0, sizeof(slots[output_index]));
            slots[output_index].kind = MP_WEATHER_SLOT_ROOM;
            slots[output_index].icon_code = UNKNOWN_ICON_CODE;
            slots[output_index].timestamp = now;
            snprintf(slots[output_index].label, sizeof(slots[output_index].label), "ROOM");
            snprintf(slots[output_index].icon, sizeof(slots[output_index].icon), "room");
            continue;
        }
        if (selection->mode == MP_WEATHER_FRAME_OUTSIDE) {
            slots[output_index] = outside;
            continue;
        }

        time_t target = local_frame_target(now, selection);
        int chosen = -1;
        for (int i = 0; i < candidate_count; i++) {
            if (candidates[i].timestamp >= target) {
                chosen = i;
                break;
            }
        }
        if (chosen < 0 && candidate_count > 0) chosen = candidate_count - 1;
        if (chosen >= 0) {
            make_slot_from_hourly(candidates[chosen].entry, candidates[chosen].timestamp, now,
                                  timezone_name, &slots[output_index]);
            continue;
        }

        if (forecast_count > 0) {
            json_object *entry = json_object_array_get_idx(forecasts, 0);
            struct weather_slot *slot = &slots[output_index];
            memset(slot, 0, sizeof(*slot));
            slot->kind = MP_WEATHER_SLOT_FORECAST;
            sanitized_forecast_label(jget(entry, "period"), slot->label);
            slot->timestamp = target;
            future_date_label(slot->timestamp, now, timezone_name, slot->date_label);
            double temperature = 0.0;
            if (as_number(jget(entry, "temperature_c"), &temperature)) {
                slot->temperature_c = clamp_int((int)lround(temperature), -99, 99);
                slot->temperature_available = true;
            }
            slot->precipitation_probability_percent =
                clamp_int(rounded_json_int(jget(entry, "precip_probability_percent"), 0), 0, 100);
            if (!as_int(jget(entry, "icon_code"), &slot->icon_code))
                slot->icon_code = UNKNOWN_ICON_CODE;
            snprintf(slot->icon, sizeof(slot->icon), "%s", legacy_icon_name(slot->icon_code));
        } else {
            slots[output_index] = outside;
            slots[output_index].kind = MP_WEATHER_SLOT_FORECAST;
            short_hour_label(target, timezone_name, slots[output_index].label);
            future_date_label(target, now, timezone_name, slots[output_index].date_label);
        }
    }
    return true;
}

static bool current_hourly_number(json_object *normalized, time_t now,
                                  const char *name, double *result)
{
    json_object *hourly = jget(normalized, "hourly");
    time_t best_time = (time_t)LLONG_MAX;
    double best_value = 0.0;
    bool found = false;
    if (!hourly || !json_object_is_type(hourly, json_type_array)) return false;
    int count = json_object_array_length(hourly);
    for (int i = 0; i < count; i++) {
        json_object *entry = json_object_array_get_idx(hourly, i);
        char text[128];
        time_t timestamp;
        double value = 0.0;
        if (!as_text(jget(entry, "time"), text, sizeof(text)) ||
            !parse_iso8601(text, &timestamp) || timestamp < now - 1800 ||
            timestamp >= best_time || !as_number(jget(entry, name), &value))
            continue;
        best_time = timestamp;
        best_value = value;
        found = true;
    }
    if (found && result) *result = best_value;
    return found;
}

static int current_hourly_metric(json_object *normalized, time_t now, const char *name, int maximum)
{
    double value = 0.0;
    if (!current_hourly_number(normalized, now, name, &value)) return 0;
    return clamp_int((int)lround(value), 0, maximum);
}

struct resolved_current_conditions {
    int temperature_c;
    bool temperature_available;
    bool temperature_is_forecast;
    int humidity_percent;
    bool humidity_available;
    int precipitation_probability_percent;
    int uv_index;
};

static void resolve_current_conditions(json_object *normalized, time_t now,
                                       struct resolved_current_conditions *resolved)
{
    memset(resolved, 0, sizeof(*resolved));
    json_object *current = jget(normalized, "current");
    double value = 0.0;

    if (as_number(jget(current, "temperature_c"), &value)) {
        resolved->temperature_c = clamp_int((int)lround(value), -99, 99);
        resolved->temperature_available = true;
    } else if (current_hourly_number(normalized, now, "temperature_c", &value)) {
        resolved->temperature_c = clamp_int((int)lround(value), -99, 99);
        resolved->temperature_available = true;
        resolved->temperature_is_forecast = true;
    }

    if (as_number(jget(current, "humidity_percent"), &value)) {
        resolved->humidity_percent = clamp_int((int)lround(value), 0, 100);
        resolved->humidity_available = true;
    }

    resolved->precipitation_probability_percent =
        current_hourly_metric(normalized, now, "precip_probability_percent", 100);
    resolved->uv_index = current_hourly_metric(normalized, now, "uv_index", 99);
}

static bool weather_raw_is_valid(const unsigned char *data, size_t length)
{
    if (!data || length != WEATHER_ICON_RAW_BYTES) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned high = data[i] >> 4;
        unsigned low = data[i] & 0x0f;
        if (!((high == 0 || high == 5 || high == 10 || high == 15) &&
              (low == 0 || low == 5 || low == 10 || low == 15))) return false;
    }
    return true;
}

static bool load_sprite(const char *library_dir, int requested_code,
                        unsigned char output[WEATHER_ICON_RAW_BYTES], int *selected_code,
                        char *substitution, size_t substitution_size,
                        struct error_info *error)
{
    int code = requested_code;
    if (code < 0 || code > 48) {
        snprintf(substitution, substitution_size, "unsupported or missing icon code %d", code);
        code = UNKNOWN_ICON_CODE;
    } else substitution[0] = '\0';
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%02d.raw", library_dir, code);
    unsigned char *data = NULL;
    size_t length = 0;
    struct error_info local = {{0}};
    if (!read_file(path, &data, &length, WEATHER_ICON_RAW_BYTES, &local, NULL) || !weather_raw_is_valid(data, length)) {
        free(data);
        if (code == UNKNOWN_ICON_CODE) {
            set_error(error, "unknown weather icon sprite is unavailable or invalid");
            return false;
        }
        snprintf(substitution, substitution_size, "code %02d unavailable; used 29", code);
        code = UNKNOWN_ICON_CODE;
        snprintf(path, sizeof(path), "%s/%02d.raw", library_dir, code);
        if (!read_file(path, &data, &length, WEATHER_ICON_RAW_BYTES, error, NULL) || !weather_raw_is_valid(data, length)) {
            free(data);
            set_error(error, "unknown weather icon sprite is unavailable or invalid");
            return false;
        }
    }
    memcpy(output, data, WEATHER_ICON_RAW_BYTES);
    free(data);
    *selected_code = code;
    return true;
}

static bool prepare_icons(const struct weather_slot slots[3], const char *library_dir,
                          const char *runtime_dir, struct icon_result *result,
                          struct error_info *error)
{
    memset(result, 0, sizeof(*result));
    if (!mkdir_p(runtime_dir, 0770, error)) return false;
    for (int i = 0; i < 3; i++) {
        unsigned char raw[WEATHER_ICON_RAW_BYTES];
        char substitution[128];
        if (!load_sprite(library_dir, slots[i].icon_code, raw, &result->codes[i],
                         substitution, sizeof(substitution), error)) return false;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/slot%d.raw", runtime_dir, i);
        if (!atomic_write_bytes(path, raw, sizeof(raw), 0644, error)) return false;
        result->count++;
        if (*substitution) {
            snprintf(result->substitutions[result->substitution_count],
                     sizeof(result->substitutions[0]), "slot %d: %.100s", i, substitution);
            result->substitution_count++;
        }
    }
    return true;
}


static json_object *frame_config_json(const struct mp_weather_frames_config *config)
{
    json_object *output = json_object_new_object();
    for (int i = 0; i < 3; i++) {
        json_object *slot = json_object_new_object();
        json_object_object_add(slot, "mode", json_object_new_string(mp_weather_frame_mode_name(config->slots[i].mode)));
        json_object_object_add(slot, "offset_hours", json_object_new_int(config->slots[i].offset_hours));
        char name[8];
        snprintf(name, sizeof(name), "slot%d", i + 1);
        json_object_object_add(output, name, slot);
    }
    return output;
}

static json_object *icon_result_json(const struct icon_result *result, const char *runtime_dir)
{
    json_object *output = json_object_new_object();
    json_object_object_add(output, "source", json_object_new_string("mk-piclock OLED-native pixel-art sprites mapped from ECCC icon codes"));
    json_object_object_add(output, "format", json_object_new_string("packed-4bit-raw"));
    json_object_object_add(output, "width", json_object_new_int(32));
    json_object_object_add(output, "height", json_object_new_int(32));
    json_object *levels = json_object_new_array();
    json_object_array_add(levels, json_object_new_int(0));
    json_object_array_add(levels, json_object_new_int(5));
    json_object_array_add(levels, json_object_new_int(10));
    json_object_array_add(levels, json_object_new_int(15));
    json_object_object_add(output, "levels", levels);
    json_object_object_add(output, "runtime_directory", json_object_new_string(runtime_dir));
    json_object_object_add(output, "icon_count", json_object_new_int(result->count));
    char codes[16];
    snprintf(codes, sizeof(codes), "%02d,%02d,%02d", result->codes[0], result->codes[1], result->codes[2]);
    json_object_object_add(output, "icon_codes", json_object_new_string(codes));
    json_object_object_add(output, "icon_style", json_object_new_string("pixel-art"));
    json_object_object_add(output, "icon_substitution_count", json_object_new_int(result->substitution_count));
    json_object *substitutions = json_object_new_array();
    for (int i = 0; i < result->substitution_count; i++) json_object_array_add(substitutions, json_object_new_string(result->substitutions[i]));
    json_object_object_add(output, "icon_substitutions", substitutions);
    return output;
}

static bool append_form_field(CURL *curl, char **form, size_t *used, size_t *capacity,
                              const char *key, const char *value, struct error_info *error)
{
    char *encoded_key = curl_easy_escape(curl, key, 0);
    char *encoded_value = curl_easy_escape(curl, value, 0);
    if (!encoded_key || !encoded_value) {
        curl_free(encoded_key);
        curl_free(encoded_value);
        set_error(error, "cannot URL-encode weather payload");
        return false;
    }
    size_t needed = strlen(encoded_key) + strlen(encoded_value) + 2 + (*used ? 1 : 0);
    if (*used + needed + 1 > *capacity) {
        size_t next = (*used + needed + 1) * 2;
        char *resized = realloc(*form, next);
        if (!resized) {
            curl_free(encoded_key);
            curl_free(encoded_value);
            set_error(error, "out of memory while building weather payload");
            return false;
        }
        *form = resized;
        *capacity = next;
    }
    if (*used) (*form)[(*used)++] = '&';
    int written = snprintf(*form + *used, *capacity - *used, "%s=%s", encoded_key, encoded_value);
    *used += (size_t)written;
    curl_free(encoded_key);
    curl_free(encoded_value);
    return true;
}


static int warning_rank(json_object *warning)
{
    char priority[64] = "";
    char colour[64] = "";
    as_text(jget(warning, "priority"), priority, sizeof(priority));
    as_text(jget(warning, "colour"), colour, sizeof(colour));
    for (char *p = priority; *p; p++) *p = (char)tolower((unsigned char)*p);
    for (char *p = colour; *p; p++) *p = (char)tolower((unsigned char)*p);

    if (strstr(priority, "urgent") || strstr(priority, "extreme") || strstr(colour, "red")) return 4;
    if (strstr(priority, "high") || strstr(colour, "orange")) return 3;
    if (strstr(priority, "medium") || strstr(colour, "yellow")) return 2;
    if (strstr(priority, "low") || strstr(colour, "green")) return 1;
    return 0;
}

static void trim_ascii(char *text)
{
    if (!text || !*text) return;
    char *start = text;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != text) memmove(text, start, strlen(start) + 1);
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) text[--len] = '\0';
}

static bool ends_with_ci(const char *text, const char *suffix)
{
    if (!text || !suffix) return false;
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (suffix_len == 0 || suffix_len > text_len) return false;
    return strcasecmp(text + text_len - suffix_len, suffix) == 0;
}

static bool warning_has_ended(json_object *warning)
{
    if (!warning) return false;

    char type[64] = "";
    char description[256] = "";
    (void)as_text(jget(warning, "type"), type, sizeof(type));
    (void)as_text(jget(warning, "description"), description, sizeof(description));
    trim_ascii(type);
    trim_ascii(description);

    /*
     * ECCC can retain a warning record until expiryTime after declaring it
     * ended.  The type field is the current lifecycle state, so an ended
     * record must not remain visible merely because its cleanup expiry is
     * still in the future.  The description check protects against payload
     * variants that include the lifecycle marker only in the title.
     */
    return strcasecmp(type, "ended") == 0 ||
           ends_with_ci(description, " ended");
}

static bool warning_display_title(json_object *warning, char *out, size_t out_size)
{
    if (!warning || !out || out_size == 0) return false;
    out[0] = '\0';

    char type[64] = "";
    char description[256] = "";
    (void)as_text(jget(warning, "type"), type, sizeof(type));
    (void)as_text(jget(warning, "description"), description, sizeof(description));
    trim_ascii(type);
    trim_ascii(description);

    const char *event = NULL;
    if (description[0]) {
        char *separator = strstr(description, " - ");
        event = separator ? separator + 3 : description;
        while (*event && isspace((unsigned char)*event)) event++;
    }

    if (event && *event) {
        snprintf(out, out_size, "%s", event);
        trim_ascii(out);
        if (type[0] && !ends_with_ci(out, type)) {
            size_t used = strlen(out);
            if (used + 1 < out_size) {
                out[used++] = ' ';
                out[used] = '\0';
                snprintf(out + used, out_size - used, "%s", type);
            }
        }
    } else if (type[0]) {
        snprintf(out, out_size, "%s", type);
    }

    for (char *p = out; *p; p++) *p = (char)toupper((unsigned char)*p);
    return out[0] != '\0';
}

static bool active_warning_type(json_object *normalized, time_t now,
                                char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    json_object *warnings = jget(normalized, "warnings");
    if (!warnings || !json_object_is_type(warnings, json_type_array)) return false;

    int best_rank = -1;
    int count = json_object_array_length(warnings);
    for (int i = 0; i < count; i++) {
        json_object *warning = json_object_array_get_idx(warnings, i);
        if (!warning || !json_object_is_type(warning, json_type_object)) continue;
        if (warning_has_ended(warning)) continue;

        char title[256] = "";
        if (!warning_display_title(warning, title, sizeof(title))) continue;

        char expires_text[128] = "";
        time_t expires = 0;
        if (as_text(jget(warning, "expires_at"), expires_text, sizeof(expires_text)) &&
            parse_iso8601(expires_text, &expires) && expires <= now) continue;

        int rank = warning_rank(warning);
        if (rank > best_rank) {
            snprintf(out, out_size, "%s", title);
            best_rank = rank;
        }
    }
    return out[0] != '\0';
}

static bool push_clock_weather(const char *api_url, json_object *normalized,
                               const struct weather_slot slots[3], time_t now,
                               int timeout_seconds, const char *user_agent,
                               struct error_info *error)
{
    if (!api_url || !*api_url) return true;
    if (strncmp(api_url, "http://", 7) != 0 && strncmp(api_url, "https://", 8) != 0) {
        set_error(error, "MK_WEATHER_API_URL is invalid");
        return false;
    }
    CURL *encoder = curl_easy_init();
    if (!encoder) {
        set_error(error, "cannot initialize URL encoder");
        return false;
    }
    size_t capacity = 1024, used = 0;
    char *form = calloc(1, capacity);
    if (!form) {
        curl_easy_cleanup(encoder);
        set_error(error, "out of memory while building weather payload");
        return false;
    }
    char value[128];
    char location[256] = "";
    as_text(jget(normalized, "location"), location, sizeof(location));
    bool ok = append_form_field(encoder, &form, &used, &capacity, "location", location, error);
    char warning_type[256] = "";
    (void)active_warning_type(normalized, now, warning_type, sizeof(warning_type));
    ok = ok && append_form_field(encoder, &form, &used, &capacity, "warning_type", warning_type, error);
    json_object *current = jget(normalized, "current");
    struct resolved_current_conditions resolved;
    resolve_current_conditions(normalized, now, &resolved);
    snprintf(value, sizeof(value), "%d", resolved.temperature_c);
    ok = ok && append_form_field(encoder, &form, &used, &capacity, "current_temperature_c", value, error);
    snprintf(value, sizeof(value), "%d", resolved.temperature_available ? 1 : 0);
    ok = ok && append_form_field(encoder, &form, &used, &capacity, "current_temperature_available", value, error);
    snprintf(value, sizeof(value), "%d", resolved.temperature_is_forecast ? 1 : 0);
    ok = ok && append_form_field(encoder, &form, &used, &capacity, "current_temperature_is_forecast", value, error);
    snprintf(value, sizeof(value), "%d", resolved.humidity_percent);
    ok = ok && append_form_field(encoder, &form, &used, &capacity, "humidity_percent", value, error);
    snprintf(value, sizeof(value), "%d", resolved.humidity_available ? 1 : 0);
    ok = ok && append_form_field(encoder, &form, &used, &capacity, "humidity_available", value, error);
    snprintf(value, sizeof(value), "%d", resolved.precipitation_probability_percent);
    ok = ok && append_form_field(encoder, &form, &used, &capacity, "precipitation_probability_percent", value, error);
    snprintf(value, sizeof(value), "%d", resolved.uv_index);
    ok = ok && append_form_field(encoder, &form, &used, &capacity, "uv_index", value, error);
    char observed_text[128];
    time_t observed = now;
    if ((as_text(jget(current, "observed_at"), observed_text, sizeof(observed_text)) && parse_iso8601(observed_text, &observed)) ||
        (as_text(jget(normalized, "source_updated_at"), observed_text, sizeof(observed_text)) && parse_iso8601(observed_text, &observed))) {
        snprintf(value, sizeof(value), "%lld", (long long)observed);
    } else snprintf(value, sizeof(value), "%lld", (long long)now);
    ok = ok && append_form_field(encoder, &form, &used, &capacity, "observed_at", value, error);

    for (int i = 0; ok && i < 3; i++) {
        char key[80];
        snprintf(key, sizeof(key), "slot%d_kind", i);
        ok = append_form_field(encoder, &form, &used, &capacity, key,
                               mp_weather_slot_kind_name(slots[i].kind), error);
        snprintf(key, sizeof(key), "slot%d_label", i);
        ok = append_form_field(encoder, &form, &used, &capacity, key, slots[i].label, error);
        snprintf(key, sizeof(key), "slot%d_date_label", i);
        ok = ok && append_form_field(encoder, &form, &used, &capacity, key, slots[i].date_label, error);
        snprintf(key, sizeof(key), "slot%d_temperature_available", i);
        snprintf(value, sizeof(value), "%d", slots[i].temperature_available ? 1 : 0);
        ok = ok && append_form_field(encoder, &form, &used, &capacity, key, value, error);
        snprintf(key, sizeof(key), "slot%d_temperature_c", i);
        snprintf(value, sizeof(value), "%d", slots[i].temperature_c);
        ok = ok && append_form_field(encoder, &form, &used, &capacity, key, value, error);
        snprintf(key, sizeof(key), "slot%d_precipitation_probability_percent", i);
        snprintf(value, sizeof(value), "%d", slots[i].precipitation_probability_percent);
        ok = ok && append_form_field(encoder, &form, &used, &capacity, key, value, error);
        snprintf(key, sizeof(key), "slot%d_icon", i);
        ok = ok && append_form_field(encoder, &form, &used, &capacity, key, slots[i].icon, error);
    }
    curl_easy_cleanup(encoder);
    if (!ok) {
        free(form);
        return false;
    }

    struct memory_buffer response = {0};
    long status = 0;
    char *content_type = NULL;
    ok = curl_request(api_url, form, timeout_seconds, user_agent,
                      MAX_API_RESPONSE_BYTES, "http,https", &response,
                      &status, &content_type, error);
    free(form);
    free(content_type);
    if (!ok) {
        free(response.data);
        return false;
    }
    if (status < 200 || status >= 300) {
        set_error(error, "clock API HTTP error %ld", status);
        free(response.data);
        return false;
    }
    if (response.size > 0) {
        json_object *reply = json_tokener_parse((const char *)response.data);
        if (!reply) {
            set_error(error, "clock API returned invalid JSON");
            free(response.data);
            return false;
        }
        json_object *ok_value = jget(reply, "ok");
        if (ok_value && json_object_is_type(ok_value, json_type_boolean) && !json_object_get_boolean(ok_value)) {
            char detail[256] = "unknown error";
            as_text(jget(reply, "error"), detail, sizeof(detail));
            set_error(error, "clock API rejected weather: %s", detail);
            json_object_put(reply);
            free(response.data);
            return false;
        }
        json_object_put(reply);
    }
    free(response.data);
    return true;
}

static bool restore_cache_if_needed(const char *cache_path, const char *output_path)
{
    if (access(output_path, F_OK) == 0 || access(cache_path, R_OK) != 0) return false;
    json_object *cached = read_json_file(cache_path);
    if (!cached) return false;
    int schema = 0;
    bool valid = as_int(jget(cached, "schema_version"), &schema) && schema == 1;
    struct error_info ignored = {{0}};
    bool restored = valid && atomic_write_json(output_path, cached, &ignored);
    json_object_put(cached);
    return restored;
}

static json_object *clone_json(json_object *object)
{
    if (!object) return NULL;
    return json_tokener_parse(json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN));
}

static bool record_weather_run(const char *status_path, const char *activity_path,
                               json_object *entry, struct error_info *error)
{
    json_object *status = json_object_new_object();
    json_object_object_add(status, "schema_version", json_object_new_int(1));
    json_object_object_foreach(entry, key, value) {
        json_object_object_add(status, key, json_object_get(value));
    }
    bool ok = atomic_write_json(status_path, status, error);
    json_object_put(status);
    if (!ok) return false;

    json_object *existing = read_json_file(activity_path);
    json_object *old_entries = existing ? jget(existing, "entries") : NULL;
    json_object *activity = json_object_new_object();
    json_object *entries = json_object_new_array();
    int old_count = old_entries && json_object_is_type(old_entries, json_type_array) ? json_object_array_length(old_entries) : 0;
    int start = old_count > ACTIVITY_LIMIT - 1 ? old_count - (ACTIVITY_LIMIT - 1) : 0;
    for (int i = start; i < old_count; i++) {
        json_object *copy = clone_json(json_object_array_get_idx(old_entries, i));
        if (copy && json_object_is_type(copy, json_type_object)) json_object_array_add(entries, copy);
        else if (copy) json_object_put(copy);
    }
    json_object_array_add(entries, clone_json(entry));
    json_object_object_add(activity, "schema_version", json_object_new_int(1));
    json_object_object_add(activity, "entries", entries);
    ok = atomic_write_json(activity_path, activity, error);
    json_object_put(activity);
    if (existing) json_object_put(existing);
    return ok;
}

static void safe_record_weather_run(const char *status_path, const char *activity_path,
                                    json_object *entry)
{
    struct error_info error = {{0}};
    if (!record_weather_run(status_path, activity_path, entry, &error))
        fprintf(stderr, "mk-piclock-weather: could not update GUI activity log: %s\n", error.text);
}

static json_object *base_activity_entry(time_t started, time_t finished, bool ok,
                                        const char *result, const char *message,
                                        const char *source_url, const char *source_path,
                                        const char *location_id, const char *output_path)
{
    char started_text[32], finished_text[32];
    iso_z(started, started_text);
    iso_z(finished, finished_text);
    json_object *entry = json_object_new_object();
    json_object_object_add(entry, "run_at", json_object_new_string(finished_text));
    json_object_object_add(entry, "started_at", json_object_new_string(started_text));
    json_object_object_add(entry, "ok", json_object_new_boolean(ok));
    json_object_object_add(entry, "result", json_object_new_string(result));
    json_object_object_add(entry, "message", json_object_new_string(message));
    if (source_url) json_object_object_add(entry, "source_url", json_object_new_string(source_url));
    json_object_object_add(entry, "source_file", json_object_new_string(source_path));
    if (location_id) json_object_object_add(entry, "location_id", json_object_new_string(location_id));
    json_object_object_add(entry, "weather_file", json_object_new_string(output_path));
    return entry;
}

#ifndef WEATHER_TEST
int main(void)
{
    const char *output_path = env_value("MK_WEATHER_OUTPUT", DEFAULT_OUTPUT_PATH);
    const char *cache_path = env_value("MK_WEATHER_CACHE", DEFAULT_CACHE_PATH);
    const char *status_path = env_value("MK_WEATHER_STATUS", DEFAULT_STATUS_PATH);
    const char *activity_path = env_value("MK_WEATHER_ACTIVITY", DEFAULT_ACTIVITY_PATH);
    const char *icon_library_dir = env_value("MK_WEATHER_ICON_LIBRARY", DEFAULT_ICON_LIBRARY_DIR);
    const char *icon_runtime_dir = env_value("MK_WEATHER_ICON_RUNTIME", DEFAULT_ICON_RUNTIME_DIR);
    const char *source_path = env_value("MK_WEATHER_SOURCE_CONFIG", DEFAULT_SOURCE_CONFIG);
    const char *frame_path = env_value("MK_WEATHER_FRAME_CONFIG", DEFAULT_FRAME_CONFIG);
    const char *api_env = getenv("MK_WEATHER_API_URL");
    const char *api_url = api_env ? api_env : DEFAULT_CLOCK_API_URL;
    const char *input_file = getenv("MK_WEATHER_INPUT_FILE");
    const char *timezone_name = env_value("MK_WEATHER_TIMEZONE", DEFAULT_TIMEZONE);
    const char *user_agent = env_value("MK_WEATHER_USER_AGENT", DEFAULT_USER_AGENT);
    time_t started = time(NULL);
    char source_url[4096] = "";
    char location_id[128] = "";
    struct error_info error = {{0}};
    int timeout_seconds, api_timeout_seconds, forecast_limit, hourly_limit;
    int stale_after_seconds, expire_after_seconds;
    bool curl_ready = false;
    json_object *raw = NULL;
    json_object *normalized = NULL;

    if (!env_int("MK_WEATHER_TIMEOUT", 10, 1, 60, &timeout_seconds, &error) ||
        !env_int("MK_WEATHER_API_TIMEOUT", 5, 1, 30, &api_timeout_seconds, &error) ||
        !env_int("MK_WEATHER_FORECAST_PERIODS", 6, 0, 20, &forecast_limit, &error) ||
        !env_int("MK_WEATHER_HOURLY_PERIODS", 48, 0, 48, &hourly_limit, &error) ||
        !env_int("MK_WEATHER_STALE_AFTER", 5400, 60, 86400, &stale_after_seconds, &error) ||
        !env_int("MK_WEATHER_EXPIRE_AFTER", 21600, 60, 604800, &expire_after_seconds, &error) ||
        expire_after_seconds <= stale_after_seconds) {
        if (!*error.text) set_error(&error, "MK_WEATHER_EXPIRE_AFTER must exceed MK_WEATHER_STALE_AFTER");
        goto failure;
    }
    if (!resolve_source(source_path, source_url, sizeof(source_url), location_id, sizeof(location_id), &error)) goto failure;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        set_error(&error, "cannot initialize libcurl");
        goto failure;
    }
    curl_ready = true;
    if (input_file && *input_file) {
        raw = read_json_file(input_file);
        if (!raw) {
            set_error(&error, "cannot read valid GeoMet JSON from %s", input_file);
            goto failure;
        }
    } else {
        raw = fetch_json(source_url, timeout_seconds, user_agent, &error);
        if (!raw) goto failure;
    }
    time_t now = time(NULL);
    normalized = normalize_weather(raw, location_id, source_url, forecast_limit,
                                   hourly_limit, stale_after_seconds,
                                   expire_after_seconds, now, &error);
    if (!normalized) goto failure;

    struct mp_weather_frames_config frames;
    char frame_error[192];
    if (mp_weather_frames_read_path(frame_path, &frames, frame_error, sizeof(frame_error)) != 0) {
        set_error(&error, "%s", frame_error);
        goto failure;
    }
    struct weather_slot slots[3];
    if (!select_clock_slots(normalized, now, &frames, timezone_name, slots, &error)) goto failure;
    struct icon_result icons;
    if (!prepare_icons(slots, icon_library_dir, icon_runtime_dir, &icons, &error)) goto failure;
    json_object_object_add(normalized, "display_frame_settings", frame_config_json(&frames));
    json_object_object_add(normalized, "display_icons", icon_result_json(&icons, icon_runtime_dir));
    if (!atomic_write_json(cache_path, normalized, &error) ||
        !atomic_write_json(output_path, normalized, &error) ||
        !push_clock_weather(api_url, normalized, slots, now, api_timeout_seconds,
                            user_agent, &error)) goto failure;

    char location[256] = "unknown", condition[256] = "unknown";
    as_text(jget(normalized, "location"), location, sizeof(location));
    json_object *current = jget(normalized, "current");
    as_text(jget(current, "condition"), condition, sizeof(condition));
    struct resolved_current_conditions activity_current;
    resolve_current_conditions(normalized, now, &activity_current);
    char reading[64];
    if (activity_current.temperature_available)
        snprintf(reading, sizeof(reading), "%d C", activity_current.temperature_c);
    else
        snprintf(reading, sizeof(reading), "temperature unavailable");
    char message[512];
    const char *result = icons.substitution_count ? "success_with_warnings" : "success";
    if (icons.substitution_count) {
        snprintf(message, sizeof(message), "Updated %.120s: %s, %.180s. Pixel-art OLED icons: %d/3; %d unknown substitution(s).",
                 location, reading, condition, icons.count, icons.substitution_count);
    } else {
        snprintf(message, sizeof(message), "Updated %.120s: %s, %.180s. Pixel-art OLED icons: %d/3.",
                 location, reading, condition, icons.count);
    }
    time_t finished = time(NULL);
    json_object *entry = base_activity_entry(started, finished, true, result, message,
                                             source_url, source_path, location_id, output_path);
    json_object_object_add(entry, "location", json_object_new_string(location));
    json_object_object_add(entry, "temperature_available",
                           json_object_new_boolean(activity_current.temperature_available));
    if (activity_current.temperature_available)
        json_object_object_add(entry, "temperature_c",
                               json_object_new_int(activity_current.temperature_c));
    json_object_object_add(entry, "condition", json_object_new_string(condition));
    json_object *updated = jget(normalized, "source_updated_at");
    if (updated) json_object_object_add(entry, "source_updated_at", json_object_get(updated));
    json_object_object_add(entry, "cache_restored", json_object_new_boolean(false));
    json_object_object_add(entry, "icon_count", json_object_new_int(icons.count));
    char icon_codes[16];
    snprintf(icon_codes, sizeof(icon_codes), "%02d,%02d,%02d", icons.codes[0], icons.codes[1], icons.codes[2]);
    json_object_object_add(entry, "icon_codes", json_object_new_string(icon_codes));
    json_object_object_add(entry, "icon_style", json_object_new_string("pixel-art"));
    json_object_object_add(entry, "icon_substitution_count", json_object_new_int(icons.substitution_count));
    json_object *substitutions = json_object_new_array();
    for (int i = 0; i < icons.substitution_count; i++) json_object_array_add(substitutions, json_object_new_string(icons.substitutions[i]));
    json_object_object_add(entry, "icon_substitutions", substitutions);
    safe_record_weather_run(status_path, activity_path, entry);
    json_object_put(entry);
    printf("mk-piclock-weather: updated %s and clock API (%s: %s, %s; pixel-art icons %d/3; source %s)\n",
           output_path, location, reading, condition, icons.count, source_url);
    json_object_put(normalized);
    json_object_put(raw);
    curl_global_cleanup();
    return 0;

failure: {
        bool restored = restore_cache_if_needed(cache_path, output_path);
        time_t finished = time(NULL);
        char message[640];
        snprintf(message, sizeof(message), "%s", *error.text ? error.text : "unknown weather service error");
        json_object *entry = base_activity_entry(started, finished, false, "error", message,
                                                 *source_url ? source_url : NULL,
                                                 source_path,
                                                 *location_id ? location_id : NULL,
                                                 output_path);
        json_object_object_add(entry, "cache_restored", json_object_new_boolean(restored));
        safe_record_weather_run(status_path, activity_path, entry);
        json_object_put(entry);
        fprintf(stderr, "mk-piclock-weather: %s%s\n", message,
                restored ? "; restored last-good cache" : "");
        if (normalized) json_object_put(normalized);
        if (raw) json_object_put(raw);
        if (curl_ready) curl_global_cleanup();
        return 1;
    }
}
#endif
