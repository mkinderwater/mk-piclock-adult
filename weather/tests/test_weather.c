#define WEATHER_TEST 1
#include "../src/mk-piclock-weather.c"

#include <assert.h>

static void test_source_validation(void)
{
    struct error_info error = {{0}};
    char canonical[512], id[64];
    assert(validate_source_url(
        "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json",
        canonical, sizeof(canonical), id, sizeof(id), &error));
    assert(strcmp(id, "ab-52") == 0);
    assert(strcmp(canonical,
        "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json") == 0);
    assert(!validate_source_url("https://example.com/items/ab-52", canonical,
                                sizeof(canonical), id, sizeof(id), &error));
    assert(timezone_is_valid("America/Edmonton"));
    assert(timezone_is_valid("UTC"));
    assert(!timezone_is_valid("../etc/passwd"));
}

static void test_source_file_errors(void)
{
    struct error_info error = {{0}};
    char canonical[512], id[64];
    assert(resolve_source("/tmp/mk-clock-adult-source-does-not-exist",
                          canonical, sizeof(canonical), id, sizeof(id), &error));
    assert(strcmp(id, DEFAULT_LOCATION_ID) == 0);

    unsigned char *data = NULL;
    size_t length = 0;
    int read_errno = 0;
    memset(&error, 0, sizeof(error));
    assert(!read_file("/tmp", &data, &length, 4096, &error, &read_errno));
    assert(read_errno != 0);
    assert(read_errno != ENOENT);
    assert(error.text[0] != '\0');

    memset(&error, 0, sizeof(error));
    assert(!resolve_source("/tmp", canonical, sizeof(canonical), id, sizeof(id), &error));
    assert(error.text[0] != '\0');
}

static json_object *load_sample(void)
{
    json_object *sample = read_json_file("tests/sample-geomet.json");
    assert(sample != NULL);
    return sample;
}

static void test_normalization_and_slots(void)
{
    struct error_info error = {{0}};
    json_object *sample = load_sample();
    time_t fetched;
    assert(parse_iso8601("2026-07-04T02:00:00Z", &fetched));
    json_object *normalized = normalize_weather(
        sample, "ab-52",
        "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json",
        6, 12, 5400, 21600, fetched, &error);
    assert(normalized != NULL);

    char text[128];
    assert(as_text(jget(normalized, "location"), text, sizeof(text)));
    assert(strcmp(text, "Calgary") == 0);
    json_object *current = jget(normalized, "current");
    double temperature = 0.0;
    assert(as_number(jget(current, "temperature_c"), &temperature));
    assert(fabs(temperature - 20.3) < 0.001);
    assert(as_text(jget(current, "condition"), text, sizeof(text)));
    assert(strcmp(text, "Partly Cloudy") == 0);
    int icon_code = -1;
    assert(as_int(jget(current, "icon_code"), &icon_code));
    assert(icon_code == 2);
    assert(as_text(jget(current, "wind_direction"), text, sizeof(text)));
    assert(strcmp(text, "ESE") == 0);

    struct mp_weather_frames_config config;
    mp_weather_frames_defaults(&config);
    struct weather_slot slots[3];
    assert(select_clock_slots(normalized, fetched, &config, "UTC", slots, &error));
    assert(strcmp(slots[0].label, "ROOM") == 0);
    assert(strcmp(slots[1].label, "5AM") == 0);
    assert(strcmp(slots[2].label, "8AM") == 0);
    assert(slots[0].kind == MP_WEATHER_SLOT_ROOM);
    assert(slots[1].kind == MP_WEATHER_SLOT_FORECAST);
    assert(slots[2].kind == MP_WEATHER_SLOT_FORECAST);
    assert(strcmp(slots[0].icon, "room") == 0);
    assert(strcmp(slots[1].icon, "rain") == 0);
    assert(strcmp(slots[2].icon, "clear") == 0);
    assert(slots[0].precipitation_probability_percent == 0);
    assert(slots[1].temperature_available);
    assert(slots[2].temperature_available);
    assert(slots[1].precipitation_probability_percent == 20);
    assert(slots[2].precipitation_probability_percent == 0);
    assert(current_hourly_metric(normalized, fetched, "uv_index", 99) == 5);

    struct resolved_current_conditions resolved;
    resolve_current_conditions(normalized, fetched, &resolved);
    assert(resolved.temperature_available);
    assert(!resolved.temperature_is_forecast);
    assert(resolved.temperature_c == 20);
    assert(resolved.humidity_available);
    assert(resolved.humidity_percent == 54);
    assert(resolved.precipitation_probability_percent == 30);
    assert(resolved.uv_index == 5);

    json_object_put(normalized);
    json_object_put(sample);
}


static void test_all_panel_modes(void)
{
    struct error_info error = {{0}};
    json_object *sample = load_sample();
    time_t fetched;
    assert(parse_iso8601("2026-07-04T02:00:00Z", &fetched));
    json_object *normalized = normalize_weather(
        sample, "ab-52",
        "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json",
        6, 12, 5400, 21600, fetched, &error);
    assert(normalized != NULL);
    struct mp_weather_frames_config config = {
        .slots = {
            {MP_WEATHER_FRAME_ROOM, 1},
            {MP_WEATHER_FRAME_OUTSIDE, 1},
            {MP_WEATHER_FRAME_OFFSET, 6}
        }
    };
    struct weather_slot slots[3];
    assert(select_clock_slots(normalized, fetched, &config, "UTC", slots, &error));
    assert(slots[0].kind == MP_WEATHER_SLOT_ROOM);
    assert(slots[1].kind == MP_WEATHER_SLOT_OUTSIDE);
    assert(slots[2].kind == MP_WEATHER_SLOT_FORECAST);
    assert(strcmp(slots[0].label, "ROOM") == 0);
    assert(strcmp(slots[1].label, "OUTSIDE") == 0);
    assert(strcmp(slots[2].label, "8AM") == 0);
    json_object_put(normalized);
    json_object_put(sample);
}



static void test_duplicate_outside_slots(void)
{
    struct error_info error = {{0}};
    json_object *sample = load_sample();
    time_t fetched;
    assert(parse_iso8601("2026-07-04T02:00:00Z", &fetched));
    json_object *normalized = normalize_weather(
        sample, "ab-52",
        "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json",
        6, 12, 5400, 21600, fetched, &error);
    assert(normalized != NULL);
    struct mp_weather_frames_config config = {
        .slots = {
            {MP_WEATHER_FRAME_OUTSIDE, 1},
            {MP_WEATHER_FRAME_OUTSIDE, 1},
            {MP_WEATHER_FRAME_OUTSIDE, 1}
        }
    };
    struct weather_slot slots[3];
    assert(select_clock_slots(normalized, fetched, &config, "UTC", slots, &error));
    for (int i = 0; i < 3; i++) {
        assert(slots[i].kind == MP_WEATHER_SLOT_OUTSIDE);
        assert(strcmp(slots[i].label, "OUTSIDE") == 0);
        assert(slots[i].date_label[0] == '\0');
        assert(slots[i].temperature_available);
        assert(slots[i].temperature_c == 20);
        assert(slots[i].icon_code == 2);
    }
    json_object_put(normalized);
    json_object_put(sample);
}

static void test_duplicate_forecast_offsets(void)
{
    struct error_info error = {{0}};
    json_object *sample = load_sample();
    time_t fetched;
    assert(parse_iso8601("2026-07-04T02:00:00Z", &fetched));
    json_object *normalized = normalize_weather(
        sample, "ab-52",
        "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json",
        6, 12, 5400, 21600, fetched, &error);
    assert(normalized != NULL);
    struct mp_weather_frames_config config = {
        .slots = {
            {MP_WEATHER_FRAME_OFFSET, 3},
            {MP_WEATHER_FRAME_OFFSET, 3},
            {MP_WEATHER_FRAME_OFFSET, 3}
        }
    };
    struct weather_slot slots[3];
    assert(select_clock_slots(normalized, fetched, &config, "UTC", slots, &error));
    for (int i = 0; i < 3; i++) {
        assert(slots[i].kind == MP_WEATHER_SLOT_FORECAST);
        assert(strcmp(slots[i].label, "5AM") == 0);
        assert(slots[i].temperature_c == slots[0].temperature_c);
        assert(slots[i].icon_code == slots[0].icon_code);
    }
    json_object_put(normalized);
    json_object_put(sample);
}


static void test_empty_current_conditions_use_hourly_display_fallback(void)
{
    struct error_info error = {{0}};
    json_object *sample = load_sample();
    json_object *properties = jget(sample, "properties");
    assert(properties != NULL);
    json_object_object_add(properties, "currentConditions", json_object_new_object());

    time_t fetched;
    assert(parse_iso8601("2026-07-04T02:00:00Z", &fetched));
    json_object *normalized = normalize_weather(
        sample, "ab-52",
        "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json",
        6, 12, 5400, 21600, fetched, &error);
    assert(normalized != NULL);

    json_object *current = jget(normalized, "current");
    assert(jget(current, "temperature_c") == NULL);
    assert(jget(current, "icon_code") == NULL);

    struct mp_weather_frames_config config = {
        .slots = {
            {MP_WEATHER_FRAME_ROOM, 1},
            {MP_WEATHER_FRAME_OUTSIDE, 1},
            {MP_WEATHER_FRAME_OUTSIDE, 1}
        }
    };
    struct weather_slot slots[3];
    assert(select_clock_slots(normalized, fetched, &config, "UTC", slots, &error));
    assert(strcmp(slots[0].label, "ROOM") == 0);
    assert(strcmp(slots[1].label, "OUTSIDE") == 0);
    assert(strcmp(slots[2].label, "OUTSIDE") == 0);
    assert(slots[0].kind == MP_WEATHER_SLOT_ROOM);
    assert(slots[1].kind == MP_WEATHER_SLOT_OUTSIDE);
    assert(slots[2].kind == MP_WEATHER_SLOT_OUTSIDE);
    assert(slots[1].temperature_available);
    assert(slots[2].temperature_available);
    assert(slots[1].temperature_c == 19);
    assert(slots[2].temperature_c == 19);
    assert(slots[1].icon_code == 3);
    assert(slots[2].icon_code == 3);
    assert(strcmp(slots[1].icon, "partly") == 0);

    struct resolved_current_conditions resolved;
    resolve_current_conditions(normalized, fetched, &resolved);
    assert(resolved.temperature_available);
    assert(resolved.temperature_is_forecast);
    assert(resolved.temperature_c == 19);
    assert(!resolved.humidity_available);
    assert(resolved.humidity_percent == 0);
    assert(resolved.precipitation_probability_percent == 30);
    assert(resolved.uv_index == 5);

    json_object_put(normalized);
    json_object_put(sample);
}


static void test_missing_panel_temperatures_remain_unavailable(void)
{
    struct error_info error = {{0}};
    json_object *normalized = json_object_new_object();
    json_object_object_add(normalized, "current", json_object_new_object());
    json_object_object_add(normalized, "hourly", json_object_new_array());
    json_object_object_add(normalized, "forecast", json_object_new_array());

    time_t now;
    assert(parse_iso8601("2026-07-04T02:00:00Z", &now));
    struct mp_weather_frames_config config = {
        .slots = {
            {MP_WEATHER_FRAME_OUTSIDE, 1},
            {MP_WEATHER_FRAME_OFFSET, 3},
            {MP_WEATHER_FRAME_OFFSET, 6}
        }
    };
    struct weather_slot slots[3];
    assert(select_clock_slots(normalized, now, &config, "UTC", slots, &error));
    for (int i = 0; i < 3; i++) {
        assert(!slots[i].temperature_available);
        assert(slots[i].temperature_c == 0);
        assert(slots[i].icon_code == UNKNOWN_ICON_CODE);
    }
    assert(slots[0].kind == MP_WEATHER_SLOT_OUTSIDE);
    assert(slots[1].kind == MP_WEATHER_SLOT_FORECAST);
    assert(slots[2].kind == MP_WEATHER_SLOT_FORECAST);
    json_object_put(normalized);
}

static void test_mixed_current_and_forecast_slots(void)
{
    struct error_info error = {{0}};
    json_object *sample = load_sample();
    time_t fetched;
    assert(parse_iso8601("2026-07-04T02:00:00Z", &fetched));
    json_object *normalized = normalize_weather(
        sample, "ab-52",
        "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json",
        6, 12, 5400, 21600, fetched, &error);
    assert(normalized != NULL);
    struct mp_weather_frames_config config = {
        .slots = {
            {MP_WEATHER_FRAME_ROOM, 1},
            {MP_WEATHER_FRAME_OUTSIDE, 1},
            {MP_WEATHER_FRAME_OFFSET, 6}
        }
    };
    struct weather_slot slots[3];
    assert(select_clock_slots(normalized, fetched, &config, "UTC", slots, &error));
    assert(strcmp(slots[1].label, "OUTSIDE") == 0);
    assert(strcmp(slots[2].label, "8AM") == 0);
    assert(slots[1].temperature_c == 20);
    assert(slots[2].temperature_c != slots[1].temperature_c);
    json_object_put(normalized);
    json_object_put(sample);
}


static void test_next_day_slot_labels(void)
{
    struct error_info error = {{0}};
    json_object *sample = load_sample();
    time_t fetched;
    assert(parse_iso8601("2026-07-03T23:00:00Z", &fetched));
    json_object *normalized = normalize_weather(
        sample, "ab-52",
        "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json",
        6, 12, 5400, 21600, fetched, &error);
    assert(normalized != NULL);

    struct mp_weather_frames_config config;
    mp_weather_frames_defaults(&config);
    struct weather_slot slots[3];
    assert(select_clock_slots(normalized, fetched, &config, "UTC", slots, &error));
    assert(strcmp(slots[1].date_label, "JULY 4") == 0);
    assert(strcmp(slots[2].date_label, "JULY 4") == 0);

    json_object_put(normalized);
    json_object_put(sample);
}

static void test_future_date_labels(void)
{
    time_t now;
    time_t same_day;
    time_t next_day;
    time_t september;
    char label[16];

    assert(parse_iso8601("2026-07-22T20:00:00Z", &now));
    assert(parse_iso8601("2026-07-22T23:00:00Z", &same_day));
    assert(parse_iso8601("2026-07-23T12:00:00Z", &next_day));
    assert(parse_iso8601("2026-09-30T12:00:00Z", &september));

    future_date_label(same_day, now, "UTC", label);
    assert(strcmp(label, "") == 0);
    future_date_label(next_day, now, "UTC", label);
    assert(strcmp(label, "JULY 23") == 0);
    future_date_label(september, now, "UTC", label);
    assert(strcmp(label, "SEPTEMBER 30") == 0);
}


static void test_active_warning_selection(void)
{
    struct error_info error = {{0}};
    json_object *sample = load_sample();
    time_t now;
    assert(parse_iso8601("2026-07-04T02:00:00Z", &now));
    json_object *normalized = normalize_weather(
        sample, "ab-52",
        "https://api.weather.gc.ca/collections/citypageweather-realtime/items/ab-52?f=json",
        6, 12, 5400, 21600, now, &error);
    assert(normalized != NULL);

    char type[128];
    assert(!active_warning_type(normalized, now, type, sizeof(type)));
    assert(type[0] == '\0');

    json_object *warnings = json_object_new_array();
    json_object *expired = json_object_new_object();
    json_object_object_add(expired, "type", json_object_new_string("Tornado Warning"));
    json_object_object_add(expired, "priority", json_object_new_string("urgent"));
    json_object_object_add(expired, "expires_at", json_object_new_string("2026-07-04T01:59:59Z"));
    json_object_array_add(warnings, expired);

    json_object *advisory = json_object_new_object();
    json_object_object_add(advisory, "type", json_object_new_string("Fog Advisory"));
    json_object_object_add(advisory, "priority", json_object_new_string("low"));
    json_object_object_add(advisory, "expires_at", json_object_new_string("2026-07-04T05:00:00Z"));
    json_object_array_add(warnings, advisory);

    json_object *warning = json_object_new_object();
    json_object_object_add(warning, "type", json_object_new_string("Severe Thunderstorm Warning"));
    json_object_object_add(warning, "priority", json_object_new_string("high"));
    json_object_object_add(warning, "expires_at", json_object_new_string("2026-07-04T04:00:00Z"));
    json_object_array_add(warnings, warning);
    json_object_object_add(normalized, "warnings", warnings);

    assert(active_warning_type(normalized, now, type, sizeof(type)));
    assert(strcmp(type, "SEVERE THUNDERSTORM WARNING") == 0);

    json_object_put(normalized);
    json_object_put(sample);
}


static void test_eccc_warning_title_format(void)
{
    json_object *source_warning = json_object_new_object();

    json_object *type = json_object_new_object();
    json_object_object_add(type, "en", json_object_new_string("watch"));
    json_object_object_add(source_warning, "type", type);

    json_object *description = json_object_new_object();
    json_object_object_add(description, "en", json_object_new_string("YELLOW WATCH - SEVERE THUNDERSTORM"));
    json_object_object_add(source_warning, "description", description);

    json_object *colour = json_object_new_object();
    json_object_object_add(colour, "en", json_object_new_string("yellow"));
    json_object_object_add(source_warning, "alertColourLevel", colour);

    json_object *expiry = json_object_new_object();
    json_object_object_add(expiry, "en", json_object_new_string("2026-07-04T05:00:00Z"));
    json_object_object_add(source_warning, "expiryTime", expiry);

    json_object *normalized = json_object_new_object();
    json_object *warnings = json_object_new_array();
    json_object_array_add(warnings, normalize_warning(source_warning));
    json_object_object_add(normalized, "warnings", warnings);

    time_t now;
    assert(parse_iso8601("2026-07-04T02:00:00Z", &now));
    char title[128];
    assert(active_warning_type(normalized, now, title, sizeof(title)));
    assert(strcmp(title, "SEVERE THUNDERSTORM WATCH") == 0);

    json_object_put(normalized);
    json_object_put(source_warning);
}

static void test_eccc_warning_title_variants(void)
{
    json_object *warning = json_object_new_object();
    json_object_object_add(warning, "type", json_object_new_string("warning"));
    json_object_object_add(warning, "description", json_object_new_string("YELLOW WARNING - AIR QUALITY"));
    char title[128];
    assert(warning_display_title(warning, title, sizeof(title)));
    assert(strcmp(title, "AIR QUALITY WARNING") == 0);
    json_object_put(warning);

    warning = json_object_new_object();
    json_object_object_add(warning, "type", json_object_new_string("advisory"));
    json_object_object_add(warning, "description", json_object_new_string("Fog Advisory"));
    assert(warning_display_title(warning, title, sizeof(title)));
    assert(strcmp(title, "FOG ADVISORY") == 0);
    json_object_put(warning);
}

static void test_eccc_ended_warning_is_inactive(void)
{
    json_object *normalized = json_object_new_object();
    json_object *warnings = json_object_new_array();
    json_object *warning = json_object_new_object();

    json_object_object_add(warning, "type", json_object_new_string("ended"));
    json_object_object_add(
        warning, "description",
        json_object_new_string("YELLOW WATCH - SEVERE THUNDERSTORM ENDED"));
    json_object_object_add(
        warning, "expires_at",
        json_object_new_string("2026-07-22T05:14:30Z"));
    json_object_array_add(warnings, warning);
    json_object_object_add(normalized, "warnings", warnings);

    time_t now;
    assert(parse_iso8601("2026-07-22T04:17:33Z", &now));
    char title[128];
    assert(!active_warning_type(normalized, now, title, sizeof(title)));
    assert(title[0] == '\0');

    json_object_put(normalized);
}

static void test_icon_fallback(void)
{
    unsigned char raw[WEATHER_ICON_RAW_BYTES];
    int selected = -1;
    char substitution[128];
    struct error_info error = {{0}};
    assert(load_sprite("assets/oled-icons", 49, raw, &selected,
                       substitution, sizeof(substitution), &error));
    assert(selected == UNKNOWN_ICON_CODE);
    assert(*substitution != '\0');
    assert(weather_raw_is_valid(raw, sizeof(raw)));
}

static void test_activity_limit(void)
{
    char directory[] = "/tmp/mk-weather-activity-test.XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char status[PATH_MAX], activity[PATH_MAX];
    snprintf(status, sizeof(status), "%s/status.json", directory);
    snprintf(activity, sizeof(activity), "%s/activity.json", directory);
    struct error_info error = {{0}};
    for (int i = 0; i < 55; i++) {
        json_object *entry = json_object_new_object();
        json_object_object_add(entry, "sequence", json_object_new_int(i));
        assert(record_weather_run(status, activity, entry, &error));
        json_object_put(entry);
    }
    json_object *document = read_json_file(activity);
    assert(document != NULL);
    json_object *entries = jget(document, "entries");
    assert(json_object_array_length(entries) == ACTIVITY_LIMIT);
    int first = -1, last = -1;
    assert(as_int(jget(json_object_array_get_idx(entries, 0), "sequence"), &first));
    assert(as_int(jget(json_object_array_get_idx(entries, ACTIVITY_LIMIT - 1), "sequence"), &last));
    assert(first == 5);
    assert(last == 54);
    json_object_put(document);
    unlink(status);
    unlink(activity);
    rmdir(directory);
}

static void test_frame_config(void)
{
    char template[] = "/tmp/mk-weather-frame-test.XXXXXX";
    int fd = mkstemp(template);
    assert(fd >= 0);
    const char *content =
        "slot1_mode=room\n"
        "slot1_offset_hours=1\n"
        "slot2_mode=outside\n"
        "slot2_offset_hours=2\n"
        "slot3_mode=offset\n"
        "slot3_offset_hours=9\n";
    assert(write(fd, content, strlen(content)) == (ssize_t)strlen(content));
    close(fd);
    struct mp_weather_frames_config config;
    char frame_error[192];
    assert(mp_weather_frames_read_path(template, &config, frame_error, sizeof(frame_error)) == 0);
    assert(config.slots[0].mode == MP_WEATHER_FRAME_ROOM);
    assert(config.slots[1].mode == MP_WEATHER_FRAME_OUTSIDE);
    assert(config.slots[2].mode == MP_WEATHER_FRAME_OFFSET);
    assert(config.slots[2].offset_hours == 9);
    char serialized[384];
    assert(mp_weather_frames_serialize(&config, serialized, sizeof(serialized),
                                       frame_error, sizeof(frame_error)) > 0);
    assert(strcmp(serialized, content) == 0);
    unlink(template);

    char legacy[] = "/tmp/mk-weather-frame-legacy.XXXXXX";
    fd = mkstemp(legacy);
    assert(fd >= 0);
    const char *legacy_content =
        "slot1_mode=current\n"
        "slot1_offset_hours=4\n"
        "slot1_hour=17\n"
        "slot2_mode=offset\n"
        "slot2_offset_hours=9\n"
        "slot2_hour=22\n";
    assert(write(fd, legacy_content, strlen(legacy_content)) == (ssize_t)strlen(legacy_content));
    close(fd);
    memset(frame_error, 0, sizeof(frame_error));
    assert(mp_weather_frames_read_path(legacy, &config, frame_error, sizeof(frame_error)) == 0);
    assert(config.slots[0].mode == MP_WEATHER_FRAME_ROOM);
    assert(config.slots[1].mode == MP_WEATHER_FRAME_OUTSIDE);
    assert(config.slots[1].offset_hours == 4);
    assert(config.slots[2].mode == MP_WEATHER_FRAME_OFFSET);
    assert(config.slots[2].offset_hours == 9);
    unlink(legacy);

    char mixed[] = "/tmp/mk-weather-frame-mixed.XXXXXX";
    fd = mkstemp(mixed);
    assert(fd >= 0);
    const char *mixed_content =
        "slot1_mode=room\n"
        "slot1_hour=17\n";
    assert(write(fd, mixed_content, strlen(mixed_content)) == (ssize_t)strlen(mixed_content));
    close(fd);
    memset(frame_error, 0, sizeof(frame_error));
    assert(mp_weather_frames_read_path(mixed, &config, frame_error, sizeof(frame_error)) != 0);
    assert(frame_error[0] != '\0');
    unlink(mixed);

    char overlong[] = "/tmp/mk-weather-frame-overlong.XXXXXX";
    fd = mkstemp(overlong);
    assert(fd >= 0);
    char long_line[600];
    memset(long_line, 'x', sizeof(long_line));
    long_line[sizeof(long_line) - 2] = '\n';
    long_line[sizeof(long_line) - 1] = '\0';
    assert(write(fd, long_line, strlen(long_line)) == (ssize_t)strlen(long_line));
    close(fd);
    memset(frame_error, 0, sizeof(frame_error));
    assert(mp_weather_frames_read_path(overlong, &config, frame_error, sizeof(frame_error)) != 0);
    assert(frame_error[0] != '\0');
    unlink(overlong);
}


static void test_icon_pack(void)
{
    for (int code = 0; code <= 48; code++) {
        char path[128];
        snprintf(path, sizeof(path), "assets/oled-icons/%02d.raw", code);
        unsigned char *data = NULL;
        size_t length = 0;
        struct error_info error = {{0}};
        assert(read_file(path, &data, &length, WEATHER_ICON_RAW_BYTES, &error, NULL));
        assert(weather_raw_is_valid(data, length));
        free(data);
    }
}


static void test_strict_json(void)
{
    char template[] = "/tmp/mk-weather-strict-json.XXXXXX";
    int fd = mkstemp(template);
    assert(fd >= 0);
    const char *invalid = "{\"ok\":true} trailing";
    assert(write(fd, invalid, strlen(invalid)) == (ssize_t)strlen(invalid));
    close(fd);
    assert(read_json_file(template) == NULL);
    unlink(template);
}

static void test_atomic_json(void)
{
    char template[] = "/tmp/mk-weather-json-test.XXXXXX";
    int fd = mkstemp(template);
    assert(fd >= 0);
    close(fd);
    unlink(template);
    json_object *object = json_object_new_object();
    json_object_object_add(object, "schema_version", json_object_new_int(1));
    json_object_object_add(object, "ok", json_object_new_boolean(true));
    struct error_info error = {{0}};
    assert(atomic_write_json(template, object, &error));
    json_object *loaded = read_json_file(template);
    assert(loaded != NULL);
    int schema = 0;
    assert(as_int(jget(loaded, "schema_version"), &schema));
    assert(schema == 1);
    json_object_put(loaded);
    json_object_put(object);
    unlink(template);
}

int main(void)
{
    test_source_validation();
    test_source_file_errors();
    test_normalization_and_slots();
    test_all_panel_modes();
    test_duplicate_outside_slots();
    test_duplicate_forecast_offsets();
    test_empty_current_conditions_use_hourly_display_fallback();
    test_missing_panel_temperatures_remain_unavailable();
    test_mixed_current_and_forecast_slots();
    test_future_date_labels();
    test_next_day_slot_labels();
    test_active_warning_selection();
    test_frame_config();
    test_icon_pack();
    test_eccc_warning_title_format();
    test_eccc_warning_title_variants();
    test_eccc_ended_warning_is_inactive();
    test_icon_fallback();
    test_strict_json();
    test_atomic_json();
    test_activity_limit();
    puts("weather C tests: PASS");
    return 0;
}
