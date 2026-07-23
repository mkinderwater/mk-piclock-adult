/*
  mk-piclock.c

  Private Raspberry Pi alarm clock core daemon.

  Features:
    - SSD1322 256x64 OLED over /dev/spidev0.0
    - libgpiod GPIO for OLED DC/RST and TTP223B touch input
    - Private Unix socket control service for mk-piclock-api
    - MP3 playback using libmpg123 + ALSA PCM
    - Alarms, fonts, weather, bedtime dimming, touch input, and config

  Build both services with the supplied Makefile.
*/

#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/wireless.h>
#include <linux/spi/spidev.h>
#include <math.h>
#include <poll.h>
#include <pwd.h>
#include <alsa/asoundlib.h>
#include <mpg123.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <gpiod.h>

#include "aht10_sensor.h"
#include "compiler_attrs.h"
#include "font_catalog.h"
#include "ipc_protocol.h"
#include "util.h"

#define safe_str mp_safe_str

#define APP_NAME "mk-clock-adult-core"
#define APP_VERSION "mk-clock-adult-1.2.62"
#define DEFAULT_CLOCK_NAME "Adult Clock"
#define APP_ROOT "/opt/mk-piclock"
#define MUSIC_DIR APP_ROOT "/assets/music"
#define DEFAULT_ALARM_PATH APP_ROOT "/assets/default-alarm.mp3"
#define DEFAULT_ALARM_LABEL "Built-in Alarm"
#define MESSAGE_CHIME_PATH APP_ROOT "/assets/message-chime.mp3"
#define MESSAGE_CHIME_LABEL "Weather Warning Chime"
#define MESSAGE_CHIME_VOLUME_MAX 55
#define FONT_DIR APP_ROOT "/assets/fonts"
#define SYSTEM_DEFAULT_FONT_ID 4
#define FONT_POLICY_VERSION 1
#define CONFIG_DIR APP_ROOT "/config"
#define CONFIG_FILE CONFIG_DIR "/clock.conf"
#define LOG_FILE CONFIG_DIR "/event.log"
#define LOG_MAX_BYTES 65536
#define LOG_KEEP_BYTES 32768
#define LOG_VIEW_LINES 200

#define ROOM_SENSOR_POLL_SECONDS_DEFAULT 10
#define ROOM_SENSOR_STALE_SECONDS_DEFAULT 120


#define OLED_SPI_DEV "/dev/spidev0.0"
#define OLED_W 256
#define OLED_H 64
#define OLED_ROW_BYTES (OLED_W / 2)
#define OLED_FB_BYTES ((OLED_W * OLED_H) / 2)
#define SPI_SPEED_HZ 4000000
#define SPI_CHUNK 4096



#define GPIO_CHIP "/dev/gpiochip0"
#define GPIO_DC 25
#define GPIO_RST 27
#define GPIO_TOUCH 20
#define TOUCH_POLL_MS 20u
#define TOUCH_DEBOUNCE_MS 50u
#define TOUCH_LONG_PRESS_MS 3000u
#define TOUCH_DIAGNOSTIC_PRESS_MS 8000u
#define DIAGNOSTIC_SCREEN_SECONDS 30u
#define DIAGNOSTIC_REFRESH_MS 2000u

#define MAX_ALARMS 7
#define ALARM_MAX_DURATION_SECONDS 1800
#define MUSIC_FILE_MAX 256


#define CORE_RUNTIME_DIR "/run/mk-piclock"
#define CORE_SOCKET_PATH CORE_RUNTIME_DIR "/core.sock"
#define ASSET_LIST_MAX_FILES 256
#define ASSET_LIST_NAME_MAX MUSIC_FILE_MAX

static volatile sig_atomic_t g_running = 1;
static time_t g_start_time = 0;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

struct alarm_slot {
    int enabled;
    int hour;
    int min;
    int weekdays;          /* bit 0 Sunday through bit 6 Saturday */
    int start_volume;      /* 0..100 */
    int end_volume;        /* 0..100 */
    int fired_yday;
    char music_file[MUSIC_FILE_MAX]; /* empty = random uploaded MP3 */
};

struct app_state {
    int display_mode;       /* 0 clock, 1 clear, 3 diagnostics */
    int display_dirty;      /* set by API IPC thread; drawn by main OLED loop */
    int diagnostic_return_mode;
    time_t diagnostic_until;
    int global_volume;
    int bedtime_enabled;
    int bedtime_start_hour;
    int bedtime_start_min;
    int bedtime_end_hour;
    int bedtime_end_min;
    int bedtime_dim_percent;
    int weather_warning_chime_enabled;
    int weather_warning_chime_during_bedtime;
    int oled_contrast_current;
    int oled_master_current;
    int oled_brightness_current;
    int brightness_preview_percent;
    time_t brightness_preview_until;
    int audio_playing;
    char audio_file[MUSIC_FILE_MAX];
    char audio_title[MP_ID3_TEXT_MAX];
    char audio_artist[MP_ID3_TEXT_MAX];
    uint64_t audio_scroll_started_ms;
    int alarm_active;             /* 1 only while an alarm MP3 is currently playing */
    int alarm_volume_percent;     /* current alarm ramp volume, 0..100 */
    long long last_successful_alarm; /* epoch when alarm audio last opened successfully */
    struct alarm_slot alarms[MAX_ALARMS];
    int oled_ok;
    char clock_name[64];
    int oled_font;         /* 0..3 built-in, 4 automatic font detection */
    char oled_font_file[128]; /* uploaded filename or system font key, empty = built-in */
    char inside_font_file[128]; /* empty = follow clock font; otherwise system/uploaded font */
    int oled_font_size;    /* TrueType pixel size */
    int font_policy_version;
    int clock_24h_mode;    /* 0 = 12-hour, 1 = 24-hour */
    int oled_color;        /* GUI panel colour: yellow, green, or white */
    pthread_mutex_t lock;
};

static struct app_state g_state = {
    .display_mode = 0,
    .display_dirty = 1,
    .diagnostic_return_mode = 0,
    .diagnostic_until = 0,
    .global_volume = 80,
    .bedtime_enabled = 0,
    .bedtime_start_hour = 21,
    .bedtime_start_min = 0,
    .bedtime_end_hour = 7,
    .bedtime_end_min = 0,
    .bedtime_dim_percent = 35,
    .weather_warning_chime_enabled = 1,
    .weather_warning_chime_during_bedtime = 0,
    .oled_contrast_current = -1,
    .oled_master_current = -1,
    .oled_brightness_current = -1,
    .brightness_preview_percent = -1,
    .brightness_preview_until = 0,
    .audio_playing = 0,
    .audio_file = "",
    .audio_title = "",
    .audio_artist = "",
    .audio_scroll_started_ms = 0,
    .alarm_active = 0,
    .alarm_volume_percent = 0,
    .last_successful_alarm = 0,
    .oled_ok = 0,
    .clock_name = DEFAULT_CLOCK_NAME,
    .oled_font = SYSTEM_DEFAULT_FONT_ID,
    .oled_font_file = "",
    .inside_font_file = "",
    .oled_font_size = 48,
    .font_policy_version = 0,
    .clock_24h_mode = 0,
    .oled_color = MP_OLED_COLOR_GREEN,
    .lock = PTHREAD_MUTEX_INITIALIZER
};


struct weather_slot_state {
    int kind;
    char label[8];
    char date_label[16];
    int temperature_c;
    int temperature_available;
    int low_temperature_c;
    int low_temperature_available;
    int low_hour;
    int high_temperature_c;
    int high_temperature_available;
    int high_hour;
    int precipitation_probability_percent;
    int icon;
};

struct weather_dashboard_state {
    char location[64];
    int warning_count;
    char warning_descriptions[MP_WEATHER_WARNING_SLOTS][MP_WEATHER_WARNING_TEXT_MAX];
    int current_temperature_c;
    int current_temperature_available;
    int current_temperature_is_forecast;
    int humidity_percent;
    int humidity_available;
    int precipitation_probability_percent;
    int uv_index;
    time_t observed_at;
    struct weather_slot_state slots[MP_WEATHER_FORECAST_SLOTS];
    pthread_mutex_t lock;
};

struct weather_dashboard_snapshot {
    char location[64];
    int warning_count;
    char warning_descriptions[MP_WEATHER_WARNING_SLOTS][MP_WEATHER_WARNING_TEXT_MAX];
    int current_temperature_c;
    int current_temperature_available;
    int current_temperature_is_forecast;
    int humidity_percent;
    int humidity_available;
    int precipitation_probability_percent;
    int uv_index;
    time_t observed_at;
    struct weather_slot_state slots[MP_WEATHER_FORECAST_SLOTS];
};

static struct weather_dashboard_state g_weather = {
    .location = "",
    .warning_count = 0,
    .warning_descriptions = {{0}},
    .current_temperature_c = 0,
    .current_temperature_available = 0,
    .current_temperature_is_forecast = 0,
    .humidity_percent = 0,
    .humidity_available = 0,
    .precipitation_probability_percent = 0,
    .uv_index = 0,
    .observed_at = 0,
    .slots = {
        { .kind = MP_WEATHER_SLOT_ROOM, .label = "INSIDE", .temperature_available = 0, .icon = MP_WEATHER_ICON_UNKNOWN },
        { .kind = MP_WEATHER_SLOT_FORECAST, .label = "LATER", .temperature_available = 0, .icon = MP_WEATHER_ICON_UNKNOWN },
        { .kind = MP_WEATHER_SLOT_FORECAST, .label = "LATER", .temperature_available = 0, .icon = MP_WEATHER_ICON_UNKNOWN }
    },
    .lock = PTHREAD_MUTEX_INITIALIZER
};

/*
 * The active warning is owned by the OLED render thread. Weather updates may
 * replace the pending warning list at any time, but this copy is not changed
 * until the current display period reaches its natural boundary.
 */
struct weather_warning_display_state {
    char text[MP_WEATHER_WARNING_TEXT_MAX];
    uint64_t started_ms;
    int refresh_needed;
};

static struct weather_warning_display_state g_weather_warning_display = {
    .text = "",
    .started_ms = 0,
    .refresh_needed = 0
};


enum room_sensor_status {
    ROOM_SENSOR_DISABLED = 0,
    ROOM_SENSOR_WAITING = 1,
    ROOM_SENSOR_ACTIVE = 2,
    ROOM_SENSOR_STALE = 3,
    ROOM_SENSOR_ERROR = 4
};

struct room_sensor_state {
    int enabled;
    enum room_sensor_status status;
    char device[128];
    int address;
    int poll_seconds;
    int stale_seconds;
    double temperature_c;
    double humidity_percent;
    time_t measured_at;
    char error[160];
    pthread_mutex_t lock;
};

struct room_sensor_snapshot {
    int enabled;
    enum room_sensor_status status;
    char device[128];
    int address;
    int poll_seconds;
    int stale_seconds;
    double temperature_c;
    double humidity_percent;
    time_t measured_at;
    char error[160];
};

static struct room_sensor_state g_room_sensor = {
    .enabled = 1,
    .status = ROOM_SENSOR_WAITING,
    .device = MP_AHT10_DEFAULT_DEVICE,
    .address = MP_AHT10_DEFAULT_ADDRESS,
    .poll_seconds = ROOM_SENSOR_POLL_SECONDS_DEFAULT,
    .stale_seconds = ROOM_SENSOR_STALE_SECONDS_DEFAULT,
    .temperature_c = 0.0,
    .humidity_percent = 0.0,
    .measured_at = 0,
    .error = "",
    .lock = PTHREAD_MUTEX_INITIALIZER
};

struct font_cache_state {
    pthread_mutex_t lock;
    FT_Library library;
    FT_Face face;
    char loaded_file[128];
    int loaded_size;
};

static struct font_cache_state g_font = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .library = NULL,
    .face = NULL,
    .loaded_file = "",
    .loaded_size = 0
};

static char g_dashboard_fit_font[128] = "";
static int g_dashboard_fit_upper_size = 0;
static int g_dashboard_fit_selected_size = 0;
static char g_room_fit_font[128] = "";
static int g_room_fit_upper_size = 0;
static int g_room_fit_selected_size = 0;

struct audio_player_state {
    pthread_mutex_t lock;
    pthread_cond_t stopped;
    int running;
    int stop_requested;
    int alarm_mode;
    int timed_out;
    uint64_t alarm_deadline_ms;
    char file[MUSIC_FILE_MAX];
};

static struct audio_player_state g_audio = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .stopped = PTHREAD_COND_INITIALIZER,
    .running = 0,
    .stop_requested = 0,
    .alarm_mode = 0,
    .timed_out = 0,
    .alarm_deadline_ms = 0,
    .file = ""
};


struct oled_dev {
    int spi_fd;
    struct gpiod_chip *chip;
    struct gpiod_line_request *gpio_req;
    uint8_t fb[OLED_FB_BYTES];
    uint8_t prev_fb[OLED_FB_BYTES];
    uint8_t preview_fb[OLED_FB_BYTES];
    pthread_mutex_t preview_lock;
    int prev_valid;
};

static struct oled_dev g_oled = {
    .spi_fd = -1,
    .chip = NULL,
    .gpio_req = NULL,
    .preview_lock = PTHREAD_MUTEX_INITIALIZER,
    .prev_valid = 0
};

/* Drawing normally targets the physical OLED framebuffer. The preview renderer
 * can render into a thread-local scratch framebuffer so its browser preview is
 * produced by the exact same renderer without changing the physical screen. */
static _Thread_local uint8_t *g_oled_render_fb = NULL;

static uint8_t *oled_draw_fb(void) {
    return g_oled_render_fb ? g_oled_render_fb : g_oled.fb;
}

struct touch_dev {
    struct gpiod_chip *chip;
    struct gpiod_line_request *request;
    pthread_mutex_t lock;
    int ready;
    int pressed;
};

static struct touch_dev g_touch = {
    .chip = NULL,
    .request = NULL,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .ready = 0,
    .pressed = 0
};

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static int ensure_dir(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static void config_encode_to_buffer(char *out, size_t out_len, const char *in) {
    static const char hex[] = "0123456789ABCDEF";
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (!in) in = "";

    size_t j = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && j + 1 < out_len; p++) {
        unsigned char ch = *p;
        int plain = isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
        if (plain) {
            out[j++] = (char)ch;
        } else {
            if (j + 3 >= out_len) break;
            out[j++] = '%';
            out[j++] = hex[(ch >> 4) & 0x0F];
            out[j++] = hex[ch & 0x0F];
        }
    }
    out[j] = '\0';
}

static void config_decode_inplace(char *s) {
    if (!s) return;
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '%' && p[1] && p[2]) {
            int hi = hex_value(p[1]);
            int lo = hex_value(p[2]);
            if (hi >= 0 && lo >= 0) {
                *o++ = (char)((hi << 4) | lo);
                p += 2;
                continue;
            }
        }
        *o++ = *p;
    }
    *o = '\0';
}

static void log_trim_if_needed_locked(void) {
    struct stat st;
    if (stat(LOG_FILE, &st) != 0 || st.st_size <= LOG_MAX_BYTES) return;

    FILE *in = fopen(LOG_FILE, "rb");
    if (!in) return;

    long keep = LOG_KEEP_BYTES;
    if (st.st_size < keep) keep = (long)st.st_size;
    if (fseek(in, -keep, SEEK_END) != 0) {
        fclose(in);
        return;
    }

    char *buf = (char *)malloc((size_t)keep + 1);
    if (!buf) {
        fclose(in);
        return;
    }

    size_t got = fread(buf, 1, (size_t)keep, in);
    fclose(in);
    buf[got] = '\0';

    char *start = buf;
    if (got == (size_t)keep && st.st_size > keep) {
        char *nl = strchr(buf, '\n');
        if (nl && nl[1]) start = nl + 1;
    }

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", LOG_FILE);
    FILE *out = fopen(tmp_path, "wb");
    if (out) {
        fwrite(start, 1, strlen(start), out);
        fclose(out);
        rename(tmp_path, LOG_FILE);
    }
    free(buf);
}

static void app_log(const char *category, const char *fmt, ...) MP_PRINTF_LIKE(2, 3);

static void app_log(const char *category, const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    for (char *p = msg; *p; p++) {
        if (*p == '\r' || *p == '\n' || *p == '\t') *p = ' ';
    }

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    pthread_mutex_lock(&g_log_lock);
    ensure_dir(CONFIG_DIR);
    log_trim_if_needed_locked();

    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "%s [%s] %s\n", ts, category && *category ? category : "info", msg);
        fclose(f);
    }
    pthread_mutex_unlock(&g_log_lock);
}


static const char *room_sensor_status_name(enum room_sensor_status status)
{
    switch (status) {
        case ROOM_SENSOR_DISABLED: return "disabled";
        case ROOM_SENSOR_WAITING: return "waiting";
        case ROOM_SENSOR_ACTIVE: return "active";
        case ROOM_SENSOR_STALE: return "stale";
        case ROOM_SENSOR_ERROR: return "error";
        default: return "unknown";
    }
}

static int env_switch(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (!value || !*value) return fallback;
    if (strcasecmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0)
        return 1;
    if (strcasecmp(value, "0") == 0 || strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0)
        return 0;
    app_log("room-sensor", "Ignoring invalid %s=%s", name, value);
    return fallback;
}

static int env_number(const char *name, int fallback, int minimum, int maximum, int base)
{
    const char *value = getenv(name);
    if (!value || !*value) return fallback;
    char *end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, base);
    if (errno != 0 || !end || *end || parsed < minimum || parsed > maximum) {
        app_log("room-sensor", "Ignoring invalid %s=%s", name, value);
        return fallback;
    }
    return (int)parsed;
}

static void room_sensor_configure(void)
{
    pthread_mutex_lock(&g_room_sensor.lock);
    g_room_sensor.enabled = env_switch("MK_AHT10_ENABLED", 1);
    const char *device = getenv("MK_AHT10_DEVICE");
    safe_str(g_room_sensor.device, sizeof(g_room_sensor.device),
             device && *device ? device : MP_AHT10_DEFAULT_DEVICE);
    g_room_sensor.address = env_number("MK_AHT10_ADDRESS",
                                       MP_AHT10_DEFAULT_ADDRESS, 0x03, 0x77, 0);
    g_room_sensor.poll_seconds = env_number("MK_AHT10_POLL_SECONDS",
                                            ROOM_SENSOR_POLL_SECONDS_DEFAULT, 2, 300, 10);
    g_room_sensor.stale_seconds = env_number("MK_AHT10_STALE_SECONDS",
                                             ROOM_SENSOR_STALE_SECONDS_DEFAULT, 10, 3600, 10);
    if (g_room_sensor.stale_seconds < g_room_sensor.poll_seconds * 2)
        g_room_sensor.stale_seconds = g_room_sensor.poll_seconds * 2;
    g_room_sensor.status = g_room_sensor.enabled ? ROOM_SENSOR_WAITING : ROOM_SENSOR_DISABLED;
    g_room_sensor.error[0] = '\0';
    pthread_mutex_unlock(&g_room_sensor.lock);
}

static void room_sensor_snapshot(struct room_sensor_snapshot *out)
{
    if (!out) return;
    pthread_mutex_lock(&g_room_sensor.lock);
    out->enabled = g_room_sensor.enabled;
    out->status = g_room_sensor.status;
    safe_str(out->device, sizeof(out->device), g_room_sensor.device);
    out->address = g_room_sensor.address;
    out->poll_seconds = g_room_sensor.poll_seconds;
    out->stale_seconds = g_room_sensor.stale_seconds;
    out->temperature_c = g_room_sensor.temperature_c;
    out->humidity_percent = g_room_sensor.humidity_percent;
    out->measured_at = g_room_sensor.measured_at;
    safe_str(out->error, sizeof(out->error), g_room_sensor.error);
    pthread_mutex_unlock(&g_room_sensor.lock);
}

static void mark_display_dirty(void)
{
    pthread_mutex_lock(&g_state.lock);
    g_state.display_dirty = 1;
    pthread_mutex_unlock(&g_state.lock);
}

static void room_sensor_sleep(int seconds)
{
    for (int elapsed = 0; g_running && elapsed < seconds; elapsed++) sleep(1);
}

static void *room_sensor_thread_main(void *arg)
{
    (void)arg;
    struct room_sensor_snapshot config;
    room_sensor_snapshot(&config);
    if (!config.enabled) return NULL;

    app_log("room-sensor", "AHT10 polling started on %s address 0x%02X",
            config.device, config.address);

    enum room_sensor_status previous_status = ROOM_SENSOR_WAITING;
    char previous_error[160] = "";
    while (g_running) {
        struct mp_aht10_sample sample;
        char error[160] = "";
        int read_result = mp_aht10_read(config.device, config.address, &sample,
                                        error, sizeof(error));
        enum room_sensor_status next_status;
        time_t now = time(NULL);

        pthread_mutex_lock(&g_room_sensor.lock);
        if (read_result == 0) {
            g_room_sensor.temperature_c = sample.temperature_c;
            g_room_sensor.humidity_percent = sample.humidity_percent;
            g_room_sensor.measured_at = sample.measured_at;
            g_room_sensor.error[0] = '\0';
            next_status = ROOM_SENSOR_ACTIVE;
        } else {
            safe_str(g_room_sensor.error, sizeof(g_room_sensor.error), error);
            if (g_room_sensor.measured_at > 0 &&
                now - g_room_sensor.measured_at <= g_room_sensor.stale_seconds)
                next_status = ROOM_SENSOR_STALE;
            else
                next_status = ROOM_SENSOR_ERROR;
        }
        g_room_sensor.status = next_status;
        pthread_mutex_unlock(&g_room_sensor.lock);

        mark_display_dirty();

        if (next_status == ROOM_SENSOR_ACTIVE && previous_status != ROOM_SENSOR_ACTIVE) {
            app_log("room-sensor", "AHT10 ready: %.1f C, %.1f%% RH",
                    sample.temperature_c, sample.humidity_percent);
        } else if (next_status != ROOM_SENSOR_ACTIVE &&
                   (next_status != previous_status || strcmp(previous_error, error) != 0)) {
            app_log("room-sensor", "AHT10 %s: %s",
                    room_sensor_status_name(next_status), error[0] ? error : "reading unavailable");
        }
        previous_status = next_status;
        safe_str(previous_error, sizeof(previous_error), error);
        room_sensor_sleep(config.poll_seconds);
    }
    return NULL;
}

static void sanitize_clock_name(char *s) {
    if (!s) return;

    char out[64];
    size_t j = 0;
    int last_space = 1;

    for (const unsigned char *p = (const unsigned char *)s; *p && j + 1 < sizeof(out); p++) {
        unsigned char ch = *p;
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        if (ch < 32 || ch > 126) continue;
        if (ch == '<' || ch == '>' || ch == '&' || ch == '"') continue;
        if (ch == ' ') {
            if (last_space) continue;
            last_space = 1;
        } else {
            last_space = 0;
        }
        out[j++] = (char)ch;
    }

    while (j > 0 && out[j - 1] == ' ') j--;
    out[j] = '\0';

    if (!out[0]) snprintf(out, sizeof(out), "%s", DEFAULT_CLOCK_NAME);
    safe_str(s, 64, out);
}

static int safe_asset_filename(const char *name);


static int has_font_ext(const char *name) {
    const char *dot = strrchr(name ? name : "", '.');
    if (!dot) return 0;
    return strcasecmp(dot, ".ttf") == 0 || strcasecmp(dot, ".otf") == 0;
}

static int safe_asset_filename(const char *name) {
    if (!name || !*name || strlen(name) >= 120) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (const char *p = name; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (!(isalnum(ch) || ch == '.' || ch == '_' || ch == '-')) return 0;
    }
    return 1;
}

static void make_font_path(const char *file, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (mp_system_font_is_key(file)) {
        (void)mp_system_font_resolve(file, out, out_len);
        return;
    }
    snprintf(out, out_len, FONT_DIR "/%s", file && *file ? file : "");
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int has_mp3_ext(const char *name) {
    if (!name) return 0;
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot, ".mp3") == 0;
}

static void make_music_path(const char *file, char *out, size_t out_len) {
    snprintf(out, out_len, MUSIC_DIR "/%s", file && *file ? file : "");
}


enum asset_list_kind {
    ASSET_LIST_MUSIC_MP3 = 1,
    ASSET_LIST_FONT = 2
};


static int asset_file_matches_kind(const char *dir, const char *name, int kind) {
    if (!dir || !name || !safe_asset_filename(name)) return 0;
    if (kind == ASSET_LIST_MUSIC_MP3) return has_mp3_ext(name);
    if (kind == ASSET_LIST_FONT) return has_font_ext(name);
    return 0;
}

static int scan_asset_files(const char *dir, int kind, char files[][ASSET_LIST_NAME_MAX], int max_files) {
    if (!dir || !files || max_files <= 0) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && count < max_files) {
        if (!asset_file_matches_kind(dir, de->d_name, kind)) continue;
        safe_str(files[count++], ASSET_LIST_NAME_MAX, de->d_name);
    }
    closedir(d);
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcasecmp(files[i], files[j]) > 0) {
                char tmp[ASSET_LIST_NAME_MAX];
                safe_str(tmp, sizeof(tmp), files[i]);
                safe_str(files[i], ASSET_LIST_NAME_MAX, files[j]);
                safe_str(files[j], ASSET_LIST_NAME_MAX, tmp);
            }
        }
    }
    return count;
}

static int apply_default_font_selection(void) {
    char current[sizeof(g_state.oled_font_file)];
    int builtin;

    pthread_mutex_lock(&g_state.lock);
    safe_str(current, sizeof(current), g_state.oled_font_file);
    builtin = g_state.oled_font;
    pthread_mutex_unlock(&g_state.lock);

    int current_valid = 0;
    if (current[0]) {
        char path[MP_SYSTEM_FONT_PATH_MAX];
        make_font_path(current, path, sizeof(path));
        current_valid = path[0] && access(path, R_OK) == 0;
    }
    if (current_valid || (!current[0] && builtin != SYSTEM_DEFAULT_FONT_ID)) return 0;

    char candidate[sizeof(g_state.oled_font_file)] = "";
    char files[ASSET_LIST_MAX_FILES][ASSET_LIST_NAME_MAX];
    int uploaded_count = scan_asset_files(FONT_DIR, ASSET_LIST_FONT, files, ASSET_LIST_MAX_FILES);
    if (uploaded_count > 0) {
        safe_str(candidate, sizeof(candidate), files[0]);
    } else {
        char system_path[MP_SYSTEM_FONT_PATH_MAX];
        (void)mp_system_font_find_filename("DejaVuSansMono.ttf", candidate, sizeof(candidate),
                                           system_path, sizeof(system_path));
    }

    int changed = 0;
    pthread_mutex_lock(&g_state.lock);
    if (strcmp(g_state.oled_font_file, current) == 0 && g_state.oled_font == builtin) {
        if (strcmp(g_state.oled_font_file, candidate) != 0 ||
            g_state.oled_font != SYSTEM_DEFAULT_FONT_ID) {
            safe_str(g_state.oled_font_file, sizeof(g_state.oled_font_file), candidate);
            g_state.oled_font = SYSTEM_DEFAULT_FONT_ID;
            g_state.display_dirty = 1;
            changed = 1;
        }
    }
    pthread_mutex_unlock(&g_state.lock);
    return changed;
}

static void init_alarm_defaults(void) {
    for (int i = 0; i < MAX_ALARMS; i++) {
        g_state.alarms[i].enabled = 0;
        g_state.alarms[i].hour = 7;
        g_state.alarms[i].min = 0;
        g_state.alarms[i].weekdays = 0x7F;
        g_state.alarms[i].start_volume = 20;
        g_state.alarms[i].end_volume = 80;
        g_state.alarms[i].fired_yday = -1;
        g_state.alarms[i].music_file[0] = '\0';
    }
}

static void reset_persistent_state_locked(void) {
    g_state.display_mode = 0;
    g_state.display_dirty = 1;
    g_state.diagnostic_return_mode = 0;
    g_state.diagnostic_until = 0;
    g_state.global_volume = 80;
    g_state.bedtime_enabled = 0;
    g_state.bedtime_start_hour = 21;
    g_state.bedtime_start_min = 0;
    g_state.bedtime_end_hour = 7;
    g_state.bedtime_end_min = 0;
    g_state.bedtime_dim_percent = 35;
    g_state.weather_warning_chime_enabled = 1;
    g_state.weather_warning_chime_during_bedtime = 0;
    g_state.last_successful_alarm = 0;
    safe_str(g_state.clock_name, sizeof(g_state.clock_name), DEFAULT_CLOCK_NAME);
    g_state.oled_font = SYSTEM_DEFAULT_FONT_ID;
    g_state.oled_font_file[0] = '\0';
    g_state.inside_font_file[0] = '\0';
    g_state.oled_font_size = 48;
    g_state.font_policy_version = 0;
    g_state.clock_24h_mode = 0;
    g_state.oled_color = MP_OLED_COLOR_GREEN;
    init_alarm_defaults();
}


#define CONFIG_INT_FIELDS(X) \
    X("global_volume", g_state.global_volume, "%d") \
    X("bedtime_enabled", g_state.bedtime_enabled, "%d") \
    X("bedtime_start_hour", g_state.bedtime_start_hour, "%02d") \
    X("bedtime_start_min", g_state.bedtime_start_min, "%02d") \
    X("bedtime_end_hour", g_state.bedtime_end_hour, "%02d") \
    X("bedtime_end_min", g_state.bedtime_end_min, "%02d") \
    X("bedtime_dim_percent", g_state.bedtime_dim_percent, "%d") \
    X("weather_warning_chime_enabled", g_state.weather_warning_chime_enabled, "%d") \
    X("weather_warning_chime_during_bedtime", g_state.weather_warning_chime_during_bedtime, "%d") \
    X("oled_font", g_state.oled_font, "%d") \
    X("oled_font_size", g_state.oled_font_size, "%d") \
    X("font_policy_version", g_state.font_policy_version, "%d") \
    X("clock_24h_mode", g_state.clock_24h_mode, "%d") \
    X("oled_color", g_state.oled_color, "%d")

#define CONFIG_STRING_FIELDS(X) \
    X("clock_name", g_state.clock_name, sizeof(g_state.clock_name)) \
    X("oled_font_file", g_state.oled_font_file, sizeof(g_state.oled_font_file)) \
    X("inside_font_file", g_state.inside_font_file, sizeof(g_state.inside_font_file))

#define CONFIG_ALARM_INT_FIELDS(X) \
    for (int i = 0; i < MAX_ALARMS; i++) { \
        X(i, "enabled", g_state.alarms[i].enabled, "%d", atoi(val) ? 1 : 0) \
        X(i, "hour", g_state.alarms[i].hour, "%02d", atoi(val)) \
        X(i, "min", g_state.alarms[i].min, "%02d", atoi(val)) \
        X(i, "weekdays", g_state.alarms[i].weekdays, "%d", atoi(val) & 0x7F) \
        X(i, "start_volume", g_state.alarms[i].start_volume, "%d", atoi(val)) \
        X(i, "end_volume", g_state.alarms[i].end_volume, "%d", atoi(val)) \
    }

#define CONFIG_ALARM_STRING_FIELDS(X) \
    for (int i = 0; i < MAX_ALARMS; i++) { \
        X(i, "music_file", g_state.alarms[i].music_file, sizeof(g_state.alarms[i].music_file)) \
    }

static void save_config(void) {
    ensure_dir(CONFIG_DIR);

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), CONFIG_FILE ".tmp");

    FILE *f = fopen(tmp_path, "w");
    if (!f) return;

    pthread_mutex_lock(&g_state.lock);
fprintf(f, "last_successful_alarm=%lld\n", g_state.last_successful_alarm);
#define SAVE_CONFIG_INT(cfg_key, field, fmt) fprintf(f, cfg_key "=" fmt "\n", field);
    CONFIG_INT_FIELDS(SAVE_CONFIG_INT)
#undef SAVE_CONFIG_INT
#define SAVE_CONFIG_ALARM_INT(idx, field_str, field, fmt, conversion) \
    fprintf(f, "alarm%d_%s=" fmt "\n", (idx) + 1, field_str, field);
    CONFIG_ALARM_INT_FIELDS(SAVE_CONFIG_ALARM_INT)
#undef SAVE_CONFIG_ALARM_INT
#define SAVE_CONFIG_ALARM_STRING(idx, field_str, field, field_size) \
    do { \
        char enc[(field_size) * 3]; \
        config_encode_to_buffer(enc, sizeof(enc), field); \
        fprintf(f, "alarm%d_%s=%s\n", (idx) + 1, field_str, enc); \
    } while (0);
    CONFIG_ALARM_STRING_FIELDS(SAVE_CONFIG_ALARM_STRING)
#undef SAVE_CONFIG_ALARM_STRING
#define SAVE_CONFIG_STRING(cfg_key, field, field_size) \
    do { \
        char enc[(field_size) * 3]; \
        config_encode_to_buffer(enc, sizeof(enc), field); \
        fprintf(f, cfg_key "=%s\n", enc); \
    } while (0);
    CONFIG_STRING_FIELDS(SAVE_CONFIG_STRING)
#undef SAVE_CONFIG_STRING
    pthread_mutex_unlock(&g_state.lock);

    int ok = 1;
    if (fflush(f) != 0) ok = 0;
    if (ok && fsync(fileno(f)) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0;

    if (ok) {
        if (rename(tmp_path, CONFIG_FILE) != 0) unlink(tmp_path);
    } else {
        unlink(tmp_path);
    }
}

static void load_config(void) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        val[strcspn(val, "\r\n")] = '\0';

        int matched = 0;
        char tmp_key[128];
        if (strcmp(key, "last_successful_alarm") == 0) {
            char *end = NULL;
            errno = 0;
            long long parsed = strtoll(val, &end, 10);
            if (!errno && end != val && *end == '\0' && parsed >= 0)
                g_state.last_successful_alarm = parsed;
            matched = 1;
        }
#define LOAD_CONFIG_ALARM_INT(idx, field_str, field, fmt, conversion) \
        do { \
            snprintf(tmp_key, sizeof(tmp_key), "alarm%d_%s", (idx) + 1, field_str); \
            if (!matched && strcmp(key, tmp_key) == 0) { \
                field = conversion; \
                matched = 1; \
            } \
        } while (0);
        CONFIG_ALARM_INT_FIELDS(LOAD_CONFIG_ALARM_INT)
#undef LOAD_CONFIG_ALARM_INT
#define LOAD_CONFIG_ALARM_STRING(idx, field_str, field, field_size) \
        do { \
            snprintf(tmp_key, sizeof(tmp_key), "alarm%d_%s", (idx) + 1, field_str); \
            if (!matched && strcmp(key, tmp_key) == 0) { \
                char dec[384]; \
                safe_str(dec, sizeof(dec), val); \
                config_decode_inplace(dec); \
                safe_str(field, field_size, dec); \
                matched = 1; \
            } \
        } while (0);
        CONFIG_ALARM_STRING_FIELDS(LOAD_CONFIG_ALARM_STRING)
#undef LOAD_CONFIG_ALARM_STRING

#define LOAD_CONFIG_INT(cfg_key, field, fmt) \
        do { \
            if (!matched && strcmp(key, cfg_key) == 0) { \
                field = atoi(val); \
                matched = 1; \
            } \
        } while (0);
        CONFIG_INT_FIELDS(LOAD_CONFIG_INT)
#undef LOAD_CONFIG_INT
#define LOAD_CONFIG_STRING(cfg_key, field, field_size) \
        do { \
            if (!matched && strcmp(key, cfg_key) == 0) { \
                char dec[384]; \
                safe_str(dec, sizeof(dec), val); \
                config_decode_inplace(dec); \
                safe_str(field, field_size, dec); \
                matched = 1; \
            } \
        } while (0);
        CONFIG_STRING_FIELDS(LOAD_CONFIG_STRING)
#undef LOAD_CONFIG_STRING
    }

    fclose(f);

    g_state.global_volume = clamp_int(g_state.global_volume, 0, 100);
    g_state.bedtime_enabled = g_state.bedtime_enabled ? 1 : 0;
    g_state.bedtime_start_hour = clamp_int(g_state.bedtime_start_hour, 0, 23);
    g_state.bedtime_start_min = clamp_int(g_state.bedtime_start_min, 0, 59);
    g_state.bedtime_end_hour = clamp_int(g_state.bedtime_end_hour, 0, 23);
    g_state.bedtime_end_min = clamp_int(g_state.bedtime_end_min, 0, 59);
    g_state.bedtime_dim_percent = clamp_int(g_state.bedtime_dim_percent, 0, 100);
    g_state.weather_warning_chime_enabled = g_state.weather_warning_chime_enabled ? 1 : 0;
    g_state.weather_warning_chime_during_bedtime = g_state.weather_warning_chime_during_bedtime ? 1 : 0;
    g_state.clock_24h_mode = g_state.clock_24h_mode ? 1 : 0;
    g_state.oled_color = clamp_int(g_state.oled_color, MP_OLED_COLOR_YELLOW, MP_OLED_COLOR_WHITE);
    if (g_state.last_successful_alarm < 0) g_state.last_successful_alarm = 0;

    for (int i = 0; i < MAX_ALARMS; i++) {
        struct alarm_slot *a = &g_state.alarms[i];
        a->enabled = a->enabled ? 1 : 0;
        a->hour = clamp_int(a->hour, 0, 23);
        a->min = clamp_int(a->min, 0, 59);
        a->weekdays &= 0x7F;
        if (a->weekdays == 0) a->weekdays = 0x7F;
        a->start_volume = clamp_int(a->start_volume, 0, 100);
        a->end_volume = clamp_int(a->end_volume, 0, 100);
        a->fired_yday = -1;
        if (a->music_file[0] && (!safe_asset_filename(a->music_file) || !has_mp3_ext(a->music_file))) {
            a->music_file[0] = '\0';
        }
    }

    sanitize_clock_name(g_state.clock_name);
    if (g_state.font_policy_version < FONT_POLICY_VERSION) {
        if (!g_state.oled_font_file[0] && g_state.oled_font == 0)
            g_state.oled_font = SYSTEM_DEFAULT_FONT_ID;
        g_state.font_policy_version = FONT_POLICY_VERSION;
    }
    if (g_state.oled_font < 0 || g_state.oled_font > SYSTEM_DEFAULT_FONT_ID)
        g_state.oled_font = SYSTEM_DEFAULT_FONT_ID;
    if (g_state.oled_font_size < 18 || g_state.oled_font_size > 54) g_state.oled_font_size = 48;
    if (g_state.inside_font_file[0]) {
        char path[MP_SYSTEM_FONT_PATH_MAX];
        make_font_path(g_state.inside_font_file, path, sizeof(path));
        if (!path[0] || access(path, R_OK) != 0) g_state.inside_font_file[0] = '\0';
    }
}

/* ---------------- OLED low level ---------------- */

static int gpio_set(unsigned int offset, int value) {
    if (!g_oled.gpio_req) return -1;
    return gpiod_line_request_set_value(
        g_oled.gpio_req,
        offset,
        value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE
    );
}

static int spi_write_bytes(const uint8_t *data, size_t len) {
    if (g_oled.spi_fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        size_t n = len - off;
        if (n > SPI_CHUNK) n = SPI_CHUNK;

        struct spi_ioc_transfer tr;
        memset(&tr, 0, sizeof(tr));
        tr.tx_buf = (unsigned long)(uintptr_t)(data + off);
        tr.len = (uint32_t)n;
        tr.speed_hz = SPI_SPEED_HZ;
        tr.bits_per_word = 8;

        if (ioctl(g_oled.spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) return -1;
        off += n;
    }
    return 0;
}

static int oled_cmd(uint8_t cmd) {
    gpio_set(GPIO_DC, 0);
    return spi_write_bytes(&cmd, 1);
}

static int oled_data(const uint8_t *data, size_t len) {
    gpio_set(GPIO_DC, 1);
    return spi_write_bytes(data, len);
}

static int oled_cmd1(uint8_t cmd, uint8_t a) {
    if (oled_cmd(cmd) != 0) return -1;
    return oled_data(&a, 1);
}

static int oled_cmd2(uint8_t cmd, uint8_t a, uint8_t b) {
    uint8_t d[2] = {a, b};
    if (oled_cmd(cmd) != 0) return -1;
    return oled_data(d, 2);
}

static int oled_set_brightness_percent(int percent) {
    percent = clamp_int(percent, 0, 100);
    if (g_oled.spi_fd < 0) return -1;

    /* SSD1322 brightness is much more obvious when both current controls move.
       0xC1 = contrast current, 0xC7 = master current. */
    /* Truncate instead of rounding at the low end. At 10%, rounding made
       both the master current and a white pixel level 2, causing a large
       jump above 0%. Truncation gives level 1 and minimum master current. */
    int hardware_percent = percent <= 0 ? 1 : percent;
    int contrast = (127 * hardware_percent) / 100;
    int master = (15 * hardware_percent) / 100;
    contrast = clamp_int(contrast, 1, 127);
    master = clamp_int(master, 1, 15);

    if (oled_cmd1(0xC1, (uint8_t)contrast) != 0) return -1;
    if (oled_cmd1(0xC7, (uint8_t)master) != 0) return -1;

    pthread_mutex_lock(&g_state.lock);
    g_state.oled_contrast_current = contrast;
    g_state.oled_master_current = master;
    g_state.oled_brightness_current = percent;
    g_state.display_dirty = 1;
    pthread_mutex_unlock(&g_state.lock);
    g_oled.prev_valid = 0;

    fprintf(stderr, "OLED brightness set: %d%% contrast=%d master=%d software-scale=%d%%\n", percent, contrast, master, percent);
    return 0;
}

static int time_in_window_minutes(int now_min, int start_min, int end_min) {
    if (start_min == end_min) return 0;
    if (start_min < end_min) return now_min >= start_min && now_min < end_min;
    return now_min >= start_min || now_min < end_min;
}

static void apply_bedtime_brightness(void) {
    int bedtime_enabled, sh, sm, eh, em, dim_pct, current_pct;
    int preview_pct;
    time_t preview_until;
    time_t now = time(NULL);

    pthread_mutex_lock(&g_state.lock);
    bedtime_enabled = g_state.bedtime_enabled;
    sh = g_state.bedtime_start_hour;
    sm = g_state.bedtime_start_min;
    eh = g_state.bedtime_end_hour;
    em = g_state.bedtime_end_min;
    dim_pct = g_state.bedtime_dim_percent;
    current_pct = g_state.oled_brightness_current;
    preview_pct = g_state.brightness_preview_percent;
    preview_until = g_state.brightness_preview_until;
    if (preview_until > 0 && preview_until <= now) {
        g_state.brightness_preview_percent = -1;
        g_state.brightness_preview_until = 0;
        preview_pct = -1;
        preview_until = 0;
    }
    pthread_mutex_unlock(&g_state.lock);

    struct tm tmv;
    localtime_r(&now, &tmv);
    int now_min = tmv.tm_hour * 60 + tmv.tm_min;
    int start_min = sh * 60 + sm;
    int end_min = eh * 60 + em;
    int in_bedtime = bedtime_enabled && time_in_window_minutes(now_min, start_min, end_min);

    /* A live GUI preview temporarily overrides the schedule. Once it expires,
       bedtime_dim_percent applies during bedtime and daytime returns to 100%. */
    int target_pct = preview_until > now && preview_pct >= 0
        ? clamp_int(preview_pct, 0, 100)
        : (in_bedtime ? clamp_int(dim_pct, 0, 100) : 100);
    if (target_pct != current_pct) oled_set_brightness_percent(target_pct);
}

static int is_bedtime_now(void) {
    int bedtime_enabled, sh, sm, eh, em;
    pthread_mutex_lock(&g_state.lock);
    bedtime_enabled = g_state.bedtime_enabled;
    sh = g_state.bedtime_start_hour;
    sm = g_state.bedtime_start_min;
    eh = g_state.bedtime_end_hour;
    em = g_state.bedtime_end_min;
    pthread_mutex_unlock(&g_state.lock);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    int now_min = tmv.tm_hour * 60 + tmv.tm_min;
    return bedtime_enabled && time_in_window_minutes(now_min, sh * 60 + sm, eh * 60 + em);
}

static void oled_clear_fb(uint8_t gray4) {
    gray4 &= 0x0F;
    memset(oled_draw_fb(), (gray4 << 4) | gray4, OLED_FB_BYTES);
}

static void oled_set_px(int x, int y, uint8_t gray4) {
    if (x < 0 || y < 0 || x >= OLED_W || y >= OLED_H) return;
    uint8_t *fb = oled_draw_fb();
    gray4 &= 0x0F;
    size_t idx = (size_t)(y * OLED_W + x) / 2;
    if ((x & 1) == 0) {
        fb[idx] = (fb[idx] & 0x0F) | (gray4 << 4);
    } else {
        fb[idx] = (fb[idx] & 0xF0) | gray4;
    }
}

static uint8_t oled_get_px(int x, int y) {
    if (x < 0 || y < 0 || x >= OLED_W || y >= OLED_H) return 0;
    const uint8_t *fb = oled_draw_fb();
    size_t idx = (size_t)(y * OLED_W + x) / 2;
    return (x & 1) == 0 ? (uint8_t)(fb[idx] >> 4) : (uint8_t)(fb[idx] & 0x0F);
}

/*
 * FreeType gives us linear 0..255 coverage, but the SSD1322 OLED and human
 * perception make mid-gray anti-aliased pixels look too bright/fat.
 *
 * Quantize coverage to 4-bit, then apply a gamma-style LUT before blending.
 * This keeps full pixels fully bright while pulling edge pixels down so uploaded
 * TrueType/OpenType fonts look sharper on the 16-level grayscale OLED.
 *
 * Index:  linear 4-bit coverage from FreeType, 0..15
 * Value:  OLED-corrected 4-bit coverage, approx gamma 2.2
 */
static void oled_fill_rect(int x, int y, int w, int h, uint8_t gray4) {
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            oled_set_px(xx, yy, gray4);
        }
    }
}

static void oled_apply_brightness_to_buffer(uint8_t *buffer, size_t len, int brightness_percent) {
    if (!buffer || len == 0) return;
    brightness_percent = clamp_int(brightness_percent < 0 ? 100 : brightness_percent, 0, 100);
    if (brightness_percent >= 100) return;

    for (size_t i = 0; i < len; i++) {
        uint8_t hi = (buffer[i] >> 4) & 0x0F;
        uint8_t lo = buffer[i] & 0x0F;
        uint8_t original_hi = hi;
        uint8_t original_lo = lo;
        hi = (uint8_t)((hi * brightness_percent) / 100);
        lo = (uint8_t)((lo * brightness_percent) / 100);
        /* Any non-zero slider value remains visible at the panel's minimum
           grayscale step instead of behaving like 0%. */
        if (brightness_percent > 0 && original_hi > 0 && hi == 0) hi = 1;
        if (brightness_percent > 0 && original_lo > 0 && lo == 0) lo = 1;
        if (hi > 15) hi = 15;
        if (lo > 15) lo = 15;
        buffer[i] = (uint8_t)((hi << 4) | lo);
    }
}

static int oled_flush_region_bytes(int byte_start, int byte_end, int row_start, int row_end) {
    static uint8_t tmp[OLED_FB_BYTES];

    if (g_oled.spi_fd < 0) return -1;

    if (byte_start < 0) byte_start = 0;
    if (byte_end >= OLED_ROW_BYTES) byte_end = OLED_ROW_BYTES - 1;
    if (row_start < 0) row_start = 0;
    if (row_end >= OLED_H) row_end = OLED_H - 1;
    if (byte_end < byte_start || row_end < row_start) return 0;

    /* SSD1322 column addressing is effectively 4 pixels wide on these modules,
       so align dirty byte ranges to 2-byte boundaries. */
    byte_start &= ~1;
    byte_end |= 1;
    if (byte_end >= OLED_ROW_BYTES) byte_end = OLED_ROW_BYTES - 1;

    int width_bytes = byte_end - byte_start + 1;
    int height = row_end - row_start + 1;
    size_t tmp_len = (size_t)width_bytes * (size_t)height;
    if (tmp_len > sizeof(tmp)) return -1;

    for (int y = 0; y < height; y++) {
        memcpy(tmp + ((size_t)y * (size_t)width_bytes),
               g_oled.fb + ((size_t)(row_start + y) * OLED_ROW_BYTES) + byte_start,
               (size_t)width_bytes);
    }

    int brightness_percent = 100;
    pthread_mutex_lock(&g_state.lock);
    brightness_percent = g_state.oled_brightness_current;
    pthread_mutex_unlock(&g_state.lock);
    oled_apply_brightness_to_buffer(tmp, tmp_len, brightness_percent);

    int col_start = 0x1C + (byte_start / 2);
    int col_end = 0x1C + (byte_end / 2);
    int rc = 0;

    if (oled_cmd2(0x15, (uint8_t)col_start, (uint8_t)col_end) != 0) rc = -1;
    else if (oled_cmd2(0x75, (uint8_t)row_start, (uint8_t)row_end) != 0) rc = -1;
    else if (oled_cmd(0x5C) != 0) rc = -1;
    else if (oled_data(tmp, tmp_len) != 0) rc = -1;

    if (rc == 0) {
        pthread_mutex_lock(&g_oled.preview_lock);
        for (int y = 0; y < height; y++) {
            memcpy(g_oled.preview_fb + ((size_t)(row_start + y) * OLED_ROW_BYTES) + byte_start,
                   tmp + ((size_t)y * (size_t)width_bytes),
                   (size_t)width_bytes);
        }
        pthread_mutex_unlock(&g_oled.preview_lock);
    }

    return rc;
}

static int oled_flush_full(void) {
    int rc = oled_flush_region_bytes(0, OLED_ROW_BYTES - 1, 0, OLED_H - 1);
    if (rc == 0) {
        memcpy(g_oled.prev_fb, g_oled.fb, sizeof(g_oled.fb));
        g_oled.prev_valid = 1;
    }
    return rc;
}

static int oled_flush(void) {
    if (g_oled.spi_fd < 0) return -1;
    if (!g_oled.prev_valid) return oled_flush_full();

    int min_row = OLED_H;
    int max_row = -1;
    int min_byte = OLED_ROW_BYTES;
    int max_byte = -1;

    for (int y = 0; y < OLED_H; y++) {
        const uint8_t *cur = g_oled.fb + ((size_t)y * OLED_ROW_BYTES);
        const uint8_t *old = g_oled.prev_fb + ((size_t)y * OLED_ROW_BYTES);
        for (int bx = 0; bx < OLED_ROW_BYTES; bx++) {
            if (cur[bx] != old[bx]) {
                if (y < min_row) min_row = y;
                if (y > max_row) max_row = y;
                if (bx < min_byte) min_byte = bx;
                if (bx > max_byte) max_byte = bx;
            }
        }
    }

    if (max_row < min_row || max_byte < min_byte) return 0;

    int rc = oled_flush_region_bytes(min_byte, max_byte, min_row, max_row);
    if (rc == 0) memcpy(g_oled.prev_fb, g_oled.fb, sizeof(g_oled.fb));
    return rc;
}

static int oled_init(void) {
    g_oled.spi_fd = open(OLED_SPI_DEV, O_RDWR);
    if (g_oled.spi_fd < 0) {
        perror("open spi");
        return -1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_SPEED_HZ;

    if (ioctl(g_oled.spi_fd, SPI_IOC_WR_MODE, &mode) < 0) perror("SPI_IOC_WR_MODE");
    if (ioctl(g_oled.spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) perror("SPI bits");
    if (ioctl(g_oled.spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) perror("SPI speed");

    g_oled.chip = gpiod_chip_open(GPIO_CHIP);
    if (!g_oled.chip) {
        perror("gpiod_chip_open");
        return -1;
    }

    unsigned int offsets[2] = { GPIO_DC, GPIO_RST };

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    struct gpiod_request_config *req_cfg = gpiod_request_config_new();

    if (!settings || !line_cfg || !req_cfg) {
        fprintf(stderr, "failed to allocate gpio request config\n");
        if (settings) gpiod_line_settings_free(settings);
        if (line_cfg) gpiod_line_config_free(line_cfg);
        if (req_cfg) gpiod_request_config_free(req_cfg);
        return -1;
    }

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);

    if (gpiod_line_config_add_line_settings(line_cfg, offsets, 2, settings) != 0) {
        perror("gpiod_line_config_add_line_settings");
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_cfg);
        gpiod_request_config_free(req_cfg);
        return -1;
    }

    gpiod_request_config_set_consumer(req_cfg, APP_NAME);

    g_oled.gpio_req = gpiod_chip_request_lines(g_oled.chip, req_cfg, line_cfg);

    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_cfg);
    gpiod_request_config_free(req_cfg);

    if (!g_oled.gpio_req) {
        perror("gpiod_chip_request_lines");
        return -1;
    }

    gpio_set(GPIO_RST, 0);
    usleep(100000);
    gpio_set(GPIO_RST, 1);
    usleep(100000);

    oled_cmd1(0xFD, 0x12);       /* unlock */
    oled_cmd(0xAE);              /* display off */
    oled_cmd1(0xB3, 0x91);       /* clock */
    oled_cmd1(0xCA, 0x3F);       /* mux ratio 64 */
    oled_cmd1(0xA2, 0x00);       /* display offset */
    oled_cmd1(0xA1, 0x00);       /* start line */
    oled_cmd2(0xA0, 0x14, 0x11); /* remap */
    oled_cmd1(0xAB, 0x01);       /* function select */
    oled_cmd1(0xB4, 0xA0);       /* display enhancement A */
    oled_data((uint8_t[]){0xFD}, 1);
    oled_cmd1(0xC1, 0x7F);       /* contrast */
    oled_cmd1(0xC7, 0x0F);       /* master contrast */
    oled_cmd1(0xB1, 0xE2);       /* phase length */
    oled_cmd1(0xD1, 0x82);       /* display enhancement B */
    oled_cmd1(0xBB, 0x1F);       /* precharge voltage */
    oled_cmd1(0xB6, 0x08);       /* second precharge */
    oled_cmd1(0xBE, 0x07);       /* VCOMH */
    oled_cmd(0xA6);              /* normal display */
    oled_cmd(0xAF);              /* display on */

    oled_clear_fb(0);
    oled_flush();
    return 0;
}

static void oled_close(void) {
    if (g_oled.spi_fd >= 0) close(g_oled.spi_fd);
    g_oled.spi_fd = -1;
    if (g_oled.gpio_req) gpiod_line_request_release(g_oled.gpio_req);
    g_oled.gpio_req = NULL;
    if (g_oled.chip) gpiod_chip_close(g_oled.chip);
}

/* ---------------- Clock drawing ---------------- */

static const uint8_t font5x7_digits[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}, /* 5 */
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}  /* 9 */
};


static uint32_t utf8_next_codepoint(const unsigned char **cursor);

static const uint8_t font5x7_unknown[7] = {0x1F,0x11,0x01,0x02,0x04,0x00,0x04};
static const uint8_t font5x7_a_umlaut[7] = {0x0A,0x00,0x0E,0x11,0x1F,0x11,0x11};
static const uint8_t font5x7_e_umlaut[7] = {0x0A,0x00,0x1F,0x10,0x1E,0x10,0x1F};
static const uint8_t font5x7_i_umlaut[7] = {0x0A,0x00,0x0E,0x04,0x04,0x04,0x0E};
static const uint8_t font5x7_o_umlaut[7] = {0x0A,0x00,0x0E,0x11,0x11,0x11,0x0E};
static const uint8_t font5x7_u_umlaut[7] = {0x0A,0x00,0x11,0x11,0x11,0x11,0x0E};
static const uint8_t font5x7_y_umlaut[7] = {0x0A,0x00,0x11,0x0A,0x04,0x04,0x04};

static const uint8_t font5x7_table[128][7] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['-'] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    [','] = {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
    ['%'] = {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    [':'] = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    ['\''] = {0x04,0x04,0x08,0x00,0x00,0x00,0x00},
    ['!'] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    ['?'] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    ['_'] = {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
    ['/'] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    ['0'] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    ['1'] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ['2'] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    ['3'] = {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    ['4'] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    ['5'] = {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    ['6'] = {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E},
    ['7'] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    ['8'] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    ['9'] = {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E},
    ['A'] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['C'] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['D'] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    ['E'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['F'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ['G'] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
    ['H'] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['I'] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['J'] = {0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    ['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M'] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    ['N'] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    ['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['Q'] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    ['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['S'] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    ['T'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['U'] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['V'] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    ['W'] = {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
    ['X'] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    ['Y'] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    ['Z'] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}
};

static const uint8_t font5x7_known[128] = {
    [' '] = 1, ['-'] = 1, ['.'] = 1, [','] = 1, ['%'] = 1,
    [':'] = 1, ['\''] = 1, ['!'] = 1, ['?'] = 1, ['_'] = 1, ['/'] = 1,
    ['0'] = 1, ['1'] = 1, ['2'] = 1, ['3'] = 1, ['4'] = 1,
    ['5'] = 1, ['6'] = 1, ['7'] = 1, ['8'] = 1, ['9'] = 1,
    ['A'] = 1, ['B'] = 1, ['C'] = 1, ['D'] = 1, ['E'] = 1,
    ['F'] = 1, ['G'] = 1, ['H'] = 1, ['I'] = 1, ['J'] = 1,
    ['K'] = 1, ['L'] = 1, ['M'] = 1, ['N'] = 1, ['O'] = 1,
    ['P'] = 1, ['Q'] = 1, ['R'] = 1, ['S'] = 1, ['T'] = 1,
    ['U'] = 1, ['V'] = 1, ['W'] = 1, ['X'] = 1, ['Y'] = 1, ['Z'] = 1
};

static const uint8_t *font5x7_glyph(uint32_t cp) {
    switch (cp) {
        case 0x00c4: case 0x00e4: return font5x7_a_umlaut;
        case 0x00cb: case 0x00eb: return font5x7_e_umlaut;
        case 0x00cf: case 0x00ef: return font5x7_i_umlaut;
        case 0x00d6: case 0x00f6: return font5x7_o_umlaut;
        case 0x00dc: case 0x00fc: return font5x7_u_umlaut;
        case 0x0178: case 0x00ff: return font5x7_y_umlaut;
        default: break;
    }

    unsigned char idx = 0;
    if (cp < 0x80) {
        idx = (unsigned char)cp;
    } else {
        switch (cp) {
            case 0x00c0: case 0x00c1: case 0x00c2: case 0x00c3: case 0x00c5:
            case 0x00e0: case 0x00e1: case 0x00e2: case 0x00e3: case 0x00e5:
            case 0x0100: case 0x0101: case 0x0102: case 0x0103: case 0x0104: case 0x0105:
            case 0x00c6: case 0x00e6:
                idx = 'A'; break;
            case 0x00c7: case 0x00e7: case 0x0106: case 0x0107: case 0x010c: case 0x010d:
                idx = 'C'; break;
            case 0x00d0: case 0x00f0: case 0x010e: case 0x010f:
                idx = 'D'; break;
            case 0x00c8: case 0x00c9: case 0x00ca:
            case 0x00e8: case 0x00e9: case 0x00ea:
            case 0x0112: case 0x0113: case 0x0116: case 0x0117: case 0x0118: case 0x0119:
                idx = 'E'; break;
            case 0x011e: case 0x011f:
                idx = 'G'; break;
            case 0x00cc: case 0x00cd: case 0x00ce:
            case 0x00ec: case 0x00ed: case 0x00ee:
            case 0x012a: case 0x012b: case 0x012e: case 0x012f:
                idx = 'I'; break;
            case 0x0139: case 0x013a: case 0x013d: case 0x013e: case 0x0141: case 0x0142:
                idx = 'L'; break;
            case 0x00d1: case 0x00f1: case 0x0143: case 0x0144: case 0x0147: case 0x0148:
                idx = 'N'; break;
            case 0x00d2: case 0x00d3: case 0x00d4: case 0x00d5: case 0x00d8:
            case 0x00f2: case 0x00f3: case 0x00f4: case 0x00f5: case 0x00f8:
            case 0x014c: case 0x014d: case 0x0150: case 0x0151: case 0x0152: case 0x0153:
                idx = 'O'; break;
            case 0x0154: case 0x0155: case 0x0158: case 0x0159:
                idx = 'R'; break;
            case 0x015a: case 0x015b: case 0x0160: case 0x0161: case 0x00df:
                idx = 'S'; break;
            case 0x0164: case 0x0165: case 0x00de: case 0x00fe:
                idx = 'T'; break;
            case 0x00d9: case 0x00da: case 0x00db:
            case 0x00f9: case 0x00fa: case 0x00fb:
            case 0x016a: case 0x016b: case 0x016e: case 0x016f: case 0x0170: case 0x0171:
                idx = 'U'; break;
            case 0x00dd: case 0x00fd:
                idx = 'Y'; break;
            case 0x0179: case 0x017a: case 0x017b: case 0x017c: case 0x017d: case 0x017e:
                idx = 'Z'; break;
            case 0x00a0:
                idx = ' '; break;
            case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212:
                idx = '-'; break;
            case 0x2018: case 0x2019: case 0x201a: case 0x201b:
            case 0x201c: case 0x201d: case 0x201e: case 0x201f:
                idx = '\''; break;
            default:
                return font5x7_unknown;
        }
    }

    if (idx >= 'a' && idx <= 'z') idx = (unsigned char)(idx - 'a' + 'A');
    if (idx < 128 && font5x7_known[idx]) return font5x7_table[idx];
    return font5x7_unknown;
}

static int text5x7_width(const char *text, int scale) {
    if (!text || !*text) return 0;
    if (scale < 1) scale = 1;
    int n = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        uint32_t cp = utf8_next_codepoint(&cursor);
        if (cp >= 0x0300 && cp <= 0x036f) continue;
        n++;
    }
    return n > 0 ? n * 5 * scale + (n - 1) * scale : 0;
}


/* Return the visible 5x7 ink span, excluding blank trailing glyph columns. */
static int text5x7_ink_width(const char *text) {
    if (!text || !*text) return 0;

    int pen_x = 0;
    int rightmost = -1;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        uint32_t cp = utf8_next_codepoint(&cursor);
        if (cp >= 0x0300 && cp <= 0x036f) continue;

        const uint8_t *glyph = font5x7_glyph(cp);
        for (int row = 0; row < 7; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 5; col++) {
                if ((bits & (1 << (4 - col))) && pen_x + col > rightmost)
                    rightmost = pen_x + col;
            }
        }
        pen_x += 6;
    }

    return rightmost >= 0 ? rightmost + 1 : 0;
}

static void draw_char5x7(int x, int y, int scale, uint32_t cp, uint8_t c) {
    const uint8_t *g = font5x7_glyph(cp);
    if (scale < 1) scale = 1;
    for (int row = 0; row < 7; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                oled_fill_rect(x + col * scale, y + row * scale, scale, scale, c);
            }
        }
    }
}

static void draw_text5x7(int x, int y, int scale, const char *text, uint8_t c) {
    if (!text) return;
    if (scale < 1) scale = 1;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        uint32_t cp = utf8_next_codepoint(&cursor);
        if (cp >= 0x0300 && cp <= 0x036f) continue;
        draw_char5x7(x, y, scale, cp, c);
        x += 6 * scale;
    }
}

static void draw_char5x7_clipped(int x, int y, uint32_t cp, uint8_t c,
                                 int clip_x0, int clip_x1) {
    const uint8_t *glyph = font5x7_glyph(cp);
    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            int px = x + col;
            if (px < clip_x0 || px >= clip_x1) continue;
            if (bits & (1 << (4 - col))) oled_set_px(px, y + row, c);
        }
    }
}

static void draw_text5x7_clipped(int x, int y, const char *text, uint8_t c,
                                 int clip_x0, int clip_x1) {
    if (!text || clip_x1 <= clip_x0) return;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        uint32_t cp = utf8_next_codepoint(&cursor);
        if (cp >= 0x0300 && cp <= 0x036f) continue;
        if (x >= clip_x1) break;
        if (x + 5 > clip_x0) draw_char5x7_clipped(x, y, cp, c, clip_x0, clip_x1);
        x += 6;
    }
}


#define WEATHER_LEFT_X0 0
#define WEATHER_LEFT_X1 102

/*
 * Dashboard width allocation on the 256-pixel OLED:
 *   clock: 103 pixels (0..102), approximately 40%
 *   forecast panels: 51 pixels each, approximately 20% each
 *
 * The divider positions are exactly 51 pixels apart throughout the weather
 * area. A divider may occupy the edge pixel of the panel to its left; content
 * is inset from each edge, so no label, icon, or temperature touches it.
 */
static const int weather_panel_x0[MP_WEATHER_FORECAST_SLOTS] = {103, 154, 205};
static const int weather_panel_x1[MP_WEATHER_FORECAST_SLOTS] = {153, 204, 255};
static const int weather_separators[] = {102, 153, 204};

/*
 * The compact Weather location occupies Y=3..7. The clock time is centred
 * on the same Y axis as the 32-pixel Weather icons, whose centre is Y=28.
 *
 * Location:      Y=3..7
 * Time region:   Y=7..48
 * Seconds line:  Y=50
 * Date region:   Y=54..60
 */
#define DASHBOARD_LOCATION_Y 3
#define DASHBOARD_CLOCK_TIME_Y0 7
#define DASHBOARD_CLOCK_TIME_Y1 48
#define DASHBOARD_SECONDS_LINE_Y 50
#define DASHBOARD_SECONDS_LINE_LEVEL 7
#define DASHBOARD_SECONDS_LINE_CLEAR_PX 3 /* clear pixels inside each usable clock-panel edge */
#define DASHBOARD_CLOCK_DATE_Y 54
#define DASHBOARD_CLOCK_CONTENT_X0 (WEATHER_LEFT_X0 + 2)
#define DASHBOARD_CLOCK_CONTENT_X1 (WEATHER_LEFT_X1 - 2)
#define DASHBOARD_FOOTER_X0 2
#define DASHBOARD_FOOTER_X1 100
#define DASHBOARD_MARQUEE_SPEED_PX_PER_SEC 18u
#define DASHBOARD_MARQUEE_GAP_PX 24
#define DASHBOARD_MARQUEE_FRAME_US 75000u
#define WEATHER_WARNING_MIN_DISPLAY_MS 12000u

static int dashboard_clock_center_x(void) {
    return (DASHBOARD_CLOCK_CONTENT_X0 + DASHBOARD_CLOCK_CONTENT_X1) / 2;
}

static void oled_draw_line(int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        oled_set_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_text5x7_centered(int x0, int x1, int y, const char *text, uint8_t c) {
    int width = text5x7_width(text, 1);
    int x = x0 + ((x1 - x0 + 1 - width) / 2);
    draw_text5x7(x, y, 1, text, c);
}

static void draw_dashboard_seconds_position_line(int second) {
    int sec = clamp_int(second, 0, 59);
    /* X=102 is the first vertical Weather separator, so the usable clock panel is X=0..101. */
    const int usable_x0 = WEATHER_LEFT_X0;
    const int usable_x1 = WEATHER_LEFT_X1 - 1;
    const int line_x0 = usable_x0 + DASHBOARD_SECONDS_LINE_CLEAR_PX;
    const int line_x1 = usable_x1 - DASHBOARD_SECONDS_LINE_CLEAR_PX;
    int gap_x = line_x0 + (sec * (line_x1 - line_x0)) / 59;

    for (int x = line_x0; x <= line_x1; x++)
        oled_set_px(x, DASHBOARD_SECONDS_LINE_Y,
                    x == gap_x ? 0 : DASHBOARD_SECONDS_LINE_LEVEL);
}

static int weather_compact_glyph(char ch, uint8_t rows[5]) {
    memset(rows, 0, 5);
    switch (ch) {
        case '0': { const uint8_t v[5] = {7,5,5,5,7}; memcpy(rows, v, 5); return 3; }
        case '1': { const uint8_t v[5] = {2,6,2,2,7}; memcpy(rows, v, 5); return 3; }
        case '2': { const uint8_t v[5] = {7,1,7,4,7}; memcpy(rows, v, 5); return 3; }
        case '3': { const uint8_t v[5] = {7,1,7,1,7}; memcpy(rows, v, 5); return 3; }
        case '4': { const uint8_t v[5] = {5,5,7,1,1}; memcpy(rows, v, 5); return 3; }
        case '5': { const uint8_t v[5] = {7,4,7,1,7}; memcpy(rows, v, 5); return 3; }
        case '6': { const uint8_t v[5] = {7,4,7,5,7}; memcpy(rows, v, 5); return 3; }
        case '7': { const uint8_t v[5] = {7,1,1,2,2}; memcpy(rows, v, 5); return 3; }
        case '8': { const uint8_t v[5] = {7,5,7,5,7}; memcpy(rows, v, 5); return 3; }
        case '9': { const uint8_t v[5] = {7,5,7,1,7}; memcpy(rows, v, 5); return 3; }
        case 'A': { const uint8_t v[5] = {2,5,7,5,5}; memcpy(rows, v, 5); return 3; }
        case 'B': { const uint8_t v[5] = {6,5,6,5,6}; memcpy(rows, v, 5); return 3; }
        case 'C': { const uint8_t v[5] = {7,4,4,4,7}; memcpy(rows, v, 5); return 3; }
        case 'D': { const uint8_t v[5] = {6,5,5,5,6}; memcpy(rows, v, 5); return 3; }
        case 'E': { const uint8_t v[5] = {7,4,6,4,7}; memcpy(rows, v, 5); return 3; }
        case 'F': { const uint8_t v[5] = {7,4,6,4,4}; memcpy(rows, v, 5); return 3; }
        case 'G': { const uint8_t v[5] = {7,4,5,5,7}; memcpy(rows, v, 5); return 3; }
        case 'H': { const uint8_t v[5] = {5,5,7,5,5}; memcpy(rows, v, 5); return 3; }
        case 'I': { const uint8_t v[5] = {7,2,2,2,7}; memcpy(rows, v, 5); return 3; }
        case 'J': { const uint8_t v[5] = {1,1,1,5,7}; memcpy(rows, v, 5); return 3; }
        case 'K': { const uint8_t v[5] = {5,5,6,5,5}; memcpy(rows, v, 5); return 3; }
        case 'L': { const uint8_t v[5] = {4,4,4,4,7}; memcpy(rows, v, 5); return 3; }
        case 'M': { const uint8_t v[5] = {5,7,7,5,5}; memcpy(rows, v, 5); return 3; }
        case 'N': { const uint8_t v[5] = {5,7,7,7,5}; memcpy(rows, v, 5); return 3; }
        case 'O': { const uint8_t v[5] = {7,5,5,5,7}; memcpy(rows, v, 5); return 3; }
        case 'P': { const uint8_t v[5] = {6,5,6,4,4}; memcpy(rows, v, 5); return 3; }
        case 'Q': { const uint8_t v[5] = {7,5,5,7,1}; memcpy(rows, v, 5); return 3; }
        case 'R': { const uint8_t v[5] = {6,5,6,5,5}; memcpy(rows, v, 5); return 3; }
        case 'S': { const uint8_t v[5] = {7,4,7,1,7}; memcpy(rows, v, 5); return 3; }
        case 'T': { const uint8_t v[5] = {7,2,2,2,2}; memcpy(rows, v, 5); return 3; }
        case 'U': { const uint8_t v[5] = {5,5,5,5,7}; memcpy(rows, v, 5); return 3; }
        case 'V': { const uint8_t v[5] = {5,5,5,5,2}; memcpy(rows, v, 5); return 3; }
        case 'W': { const uint8_t v[5] = {5,5,7,7,5}; memcpy(rows, v, 5); return 3; }
        case 'X': { const uint8_t v[5] = {5,5,2,5,5}; memcpy(rows, v, 5); return 3; }
        case 'Y': { const uint8_t v[5] = {5,5,2,2,2}; memcpy(rows, v, 5); return 3; }
        case 'Z': { const uint8_t v[5] = {7,1,2,4,7}; memcpy(rows, v, 5); return 3; }
        case '%': { const uint8_t v[5] = {5,1,2,4,5}; memcpy(rows, v, 5); return 3; }
        case '-': { const uint8_t v[5] = {0,0,7,0,0}; memcpy(rows, v, 5); return 3; }
        case '.': { const uint8_t v[5] = {0,0,0,0,1}; memcpy(rows, v, 5); return 1; }
        case '^': { const uint8_t v[5] = {2,5,2,0,0}; memcpy(rows, v, 5); return 3; }
        case '(': { const uint8_t v[5] = {1,2,2,2,1}; memcpy(rows, v, 5); return 2; }
        case ')': { const uint8_t v[5] = {2,1,1,1,2}; memcpy(rows, v, 5); return 2; }
        case ' ': return 1;
        default: return 0;
    }
}

static int weather_compact_text_width(const char *text) {
    int width = 0;
    if (!text) return 0;
    for (const char *p = text; *p; p++) {
        uint8_t rows[5];
        int glyph_width = weather_compact_glyph(*p, rows);
        if (glyph_width <= 0) continue;
        width += glyph_width + 1;
    }
    return width > 0 ? width - 1 : 0;
}

/* Centre compact text by its illuminated pixels, not only its advance box. */
static int weather_compact_text_optical_x(const char *text, int target_center_x) {
    int pen_x = 0;
    int lit_count = 0;
    int lit_x_sum = 0;

    if (!text || !*text) return target_center_x;

    for (const char *p = text; *p; p++) {
        uint8_t rows[5];
        int glyph_width = weather_compact_glyph(*p, rows);
        if (glyph_width <= 0) continue;

        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < glyph_width; col++) {
                if (rows[row] & (1u << (glyph_width - 1 - col))) {
                    lit_x_sum += pen_x + col;
                    lit_count++;
                }
            }
        }
        pen_x += glyph_width + 1;
    }

    if (lit_count == 0)
        return target_center_x - (weather_compact_text_width(text) / 2);

    int optical_center_x = (lit_x_sum + (lit_count / 2)) / lit_count;
    return target_center_x - optical_center_x;
}

static void make_weather_location_label(const char *location, char *label, size_t label_size) {
    if (!label || label_size == 0) return;
    label[0] = '\0';
    if (!location) return;

    size_t used = 0;
    int pending_space = 0;
    for (const unsigned char *p = (const unsigned char *)location; *p; p++) {
        if (*p == ',' || *p == ';' || *p == '/') break;
        if (isalnum(*p)) {
            if (pending_space && used > 0 && used + 1 < label_size) label[used++] = ' ';
            pending_space = 0;
            if (used + 1 >= label_size) break;
            label[used++] = (char)toupper(*p);
        } else if (*p == '-') {
            if (used > 0 && used + 1 < label_size) label[used++] = '-';
            pending_space = 0;
        } else if (isspace(*p)) {
            pending_space = 1;
        }
    }
    while (used > 0 && (label[used - 1] == ' ' || label[used - 1] == '-')) used--;
    label[used] = '\0';

    const int max_width = WEATHER_LEFT_X1 - WEATHER_LEFT_X0 - 4;
    while (used > 0 && weather_compact_text_width(label) > max_width) {
        label[--used] = '\0';
        while (used > 0 && label[used - 1] == ' ') label[--used] = '\0';
    }
}

static void make_weather_header_label(const char *location, int alarm_on,
                                      char *label, size_t label_size) {
    static const char alarm_suffix[] = " - ALARM ON";
    char location_label[64];
    const int max_width = WEATHER_LEFT_X1 - WEATHER_LEFT_X0 - 4;

    if (!label || label_size == 0) return;
    make_weather_location_label(location, location_label, sizeof(location_label));

    if (!alarm_on) {
        safe_str(label, label_size, location_label);
        return;
    }

    while (location_label[0]) {
        snprintf(label, label_size, "%s%s", location_label, alarm_suffix);
        if (weather_compact_text_width(label) <= max_width) return;

        size_t used = strlen(location_label);
        location_label[--used] = '\0';
        while (used > 0 &&
               (location_label[used - 1] == ' ' || location_label[used - 1] == '-'))
            location_label[--used] = '\0';
    }

    safe_str(label, label_size, "ALARM ON");
}

static void draw_weather_compact_text(int x, int y, const char *text, uint8_t c) {
    if (!text) return;
    for (const char *p = text; *p; p++) {
        uint8_t rows[5];
        int glyph_width = weather_compact_glyph(*p, rows);
        if (glyph_width <= 0) continue;
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < glyph_width; col++) {
                if (rows[row] & (1u << (glyph_width - 1 - col)))
                    oled_set_px(x + col, y + row, c);
            }
        }
        x += glyph_width + 1;
    }
}

static void draw_weather_compact_text_centered(
    int x0,
    int x1,
    int y,
    const char *text,
    uint8_t c
) {
    if (!text) return;
    int width = weather_compact_text_width(text);
    int x = x0 + ((x1 - x0 + 1 - width) / 2);
    draw_weather_compact_text(x, y, text, c);
}

static void draw_weather_compact_text_right_aligned(
    int x1,
    int y,
    const char *text,
    uint8_t c
) {
    if (!text) return;
    int width = weather_compact_text_width(text);
    draw_weather_compact_text(x1 - width + 1, y, text, c);
}

static void draw_weather_temperature_precip_centered(
    int x0,
    int x1,
    int y,
    int temperature_c,
    int temperature_available,
    int precipitation_probability_percent,
    uint8_t c
) {
    char text[32];
    int precip = clamp_int(precipitation_probability_percent, 0, 100);
    if (temperature_available) {
        if (precip > 0)
            snprintf(text, sizeof(text), "%d^ (%d%%)", temperature_c, precip);
        else
            snprintf(text, sizeof(text), "%d^", temperature_c);
    } else if (precip > 0) {
        snprintf(text, sizeof(text), "--^ (%d%%)", precip);
    } else {
        snprintf(text, sizeof(text), "--^");
    }

    draw_weather_compact_text_centered(x0, x1, y, text, c);
}

/* Shared lower-row baseline keeps TODAY POP, OUTSIDE temperature,
   forecast temperature, and INSIDE relative humidity level across panels. */
#define WEATHER_PANEL_LOWER_ROW_Y 47
#define WEATHER_TODAY_ROW_SPACING 15
#define WEATHER_TODAY_HIGH_ROW_Y \
    (WEATHER_PANEL_LOWER_ROW_Y - WEATHER_TODAY_ROW_SPACING)
#define WEATHER_TODAY_LOW_ROW_Y \
    (WEATHER_TODAY_HIGH_ROW_Y - WEATHER_TODAY_ROW_SPACING)

static void draw_weather_today_panel(
    int x0,
    int x1,
    const struct weather_slot_state *slot
) {
    if (!slot) return;

    char low_value[16];
    char high_value[16];
    char precipitation_value[16];
    const int label_x = x0 + 5;
    const int value_x1 = x1 - 5;

    if (slot->low_temperature_available)
        snprintf(low_value, sizeof(low_value), "%d^",
                 slot->low_temperature_c);
    else
        snprintf(low_value, sizeof(low_value), "--^");

    if (slot->high_temperature_available)
        snprintf(high_value, sizeof(high_value), "%d^",
                 slot->high_temperature_c);
    else
        snprintf(high_value, sizeof(high_value), "--^");

    snprintf(precipitation_value, sizeof(precipitation_value), "%d%%",
             clamp_int(slot->precipitation_probability_percent, 0, 100));

    draw_weather_compact_text(label_x, WEATHER_TODAY_LOW_ROW_Y, "L", 13);
    draw_weather_compact_text_right_aligned(
        value_x1, WEATHER_TODAY_LOW_ROW_Y, low_value, 13);
    draw_weather_compact_text(label_x, WEATHER_TODAY_HIGH_ROW_Y, "H", 13);
    draw_weather_compact_text_right_aligned(
        value_x1, WEATHER_TODAY_HIGH_ROW_Y, high_value, 13);
    draw_weather_compact_text(label_x, WEATHER_PANEL_LOWER_ROW_Y, "POP", 11);
    draw_weather_compact_text_right_aligned(
        value_x1, WEATHER_PANEL_LOWER_ROW_Y, precipitation_value, 11);
}



#define WEATHER_ICON_ASSET_SIZE 32
#define WEATHER_ICON_CENTER_Y 28
#define WEATHER_ICON_ASSET_BYTES ((WEATHER_ICON_ASSET_SIZE * WEATHER_ICON_ASSET_SIZE) / 2)
#define WEATHER_ICON_ASSET_DIR "/run/mk-piclock/weather-icons"

static int load_grayscale_icon_asset(const char *path, uint8_t *raw, size_t raw_size) {
    if (!path || !*path || !raw || raw_size != WEATHER_ICON_ASSET_BYTES) return -1;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;

    size_t used = 0;
    while (used < raw_size) {
        ssize_t count = read(fd, raw + used, raw_size - used);
        if (count < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if (count == 0) break;
        used += (size_t)count;
    }

    unsigned char extra = 0;
    ssize_t trailing = read(fd, &extra, 1);
    close(fd);
    if (used != raw_size || trailing != 0) return -1;

    for (size_t i = 0; i < raw_size; i++) {
        uint8_t high = (uint8_t)(raw[i] >> 4);
        uint8_t low = (uint8_t)(raw[i] & 0x0f);
        if (!((high == 0 || high == 5 || high == 10 || high == 15) &&
              (low == 0 || low == 5 || low == 10 || low == 15)))
            return -1;
    }
    return 0;
}

static int draw_grayscale_icon_asset(const char *path, int cx, int cy) {
    uint8_t raw[WEATHER_ICON_ASSET_BYTES];
    if (load_grayscale_icon_asset(path, raw, sizeof(raw)) != 0) return -1;

    int x0 = cx - (WEATHER_ICON_ASSET_SIZE / 2);
    int y0 = cy - (WEATHER_ICON_ASSET_SIZE / 2);
    for (int y = 0; y < WEATHER_ICON_ASSET_SIZE; y++) {
        for (int x = 0; x < WEATHER_ICON_ASSET_SIZE; x++) {
            size_t pixel = (size_t)y * WEATHER_ICON_ASSET_SIZE + (size_t)x;
            uint8_t packed = raw[pixel / 2];
            uint8_t level = (pixel & 1u) ? (uint8_t)(packed & 0x0f)
                                         : (uint8_t)(packed >> 4);
            if (level != 0) oled_set_px(x0 + x, y0 + y, level);
        }
    }
    return 0;
}

static int draw_weather_icon_asset(int slot_index, int cx, int cy) {
    if (slot_index < 0 || slot_index >= MP_WEATHER_FORECAST_SLOTS) return -1;
    char path[160];
    int written = snprintf(path, sizeof(path), WEATHER_ICON_ASSET_DIR "/slot%d.raw", slot_index);
    if (written < 0 || (size_t)written >= sizeof(path)) return -1;
    return draw_grayscale_icon_asset(path, cx, cy);
}

static void draw_weather_icon_missing(int cx, int cy) {
    int x0 = cx - 10;
    int y0 = cy - 12;
    oled_draw_line(x0, y0, x0 + 20, y0, 10);
    oled_draw_line(x0, y0 + 24, x0 + 20, y0 + 24, 10);
    oled_draw_line(x0, y0, x0, y0 + 24, 10);
    oled_draw_line(x0 + 20, y0, x0 + 20, y0 + 24, 10);
    oled_draw_line(cx - 4, cy - 6, cx, cy - 9, 15);
    oled_draw_line(cx, cy - 9, cx + 4, cy - 6, 15);
    oled_draw_line(cx + 4, cy - 6, cx + 4, cy - 2, 15);
    oled_draw_line(cx + 4, cy - 2, cx, cy + 2, 15);
    oled_draw_line(cx, cy + 2, cx, cy + 5, 15);
    oled_set_px(cx, cy + 9, 15);
}


static void weather_snapshot(struct weather_dashboard_snapshot *out) {
    if (!out) return;
    pthread_mutex_lock(&g_weather.lock);
    safe_str(out->location, sizeof(out->location), g_weather.location);
    out->warning_count = g_weather.warning_count;
    memcpy(out->warning_descriptions, g_weather.warning_descriptions,
           sizeof(out->warning_descriptions));
    out->current_temperature_c = g_weather.current_temperature_c;
    out->current_temperature_available = g_weather.current_temperature_available;
    out->current_temperature_is_forecast = g_weather.current_temperature_is_forecast;
    out->humidity_percent = g_weather.humidity_percent;
    out->humidity_available = g_weather.humidity_available;
    out->precipitation_probability_percent = g_weather.precipitation_probability_percent;
    out->uv_index = g_weather.uv_index;
    out->observed_at = g_weather.observed_at;
    memcpy(out->slots, g_weather.slots, sizeof(out->slots));
    pthread_mutex_unlock(&g_weather.lock);
}


static uint64_t monotonic_millis(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int dashboard_marquee_offset(int cycle_width, uint64_t started_ms) {
    if (cycle_width <= 0) return 0;
    uint64_t now_ms = monotonic_millis();
    uint64_t elapsed = now_ms >= started_ms ? now_ms - started_ms : 0;
    uint64_t pixels = (elapsed * DASHBOARD_MARQUEE_SPEED_PX_PER_SEC) / 1000u;
    return (int)(pixels % (uint64_t)cycle_width);
}

static void draw_dashboard_marquee_line(const char *text, uint64_t started_ms) {
    if (!text || !*text) return;

    const int clip_x0 = DASHBOARD_FOOTER_X0;
    const int clip_x1 = DASHBOARD_FOOTER_X1 + 1;
    const int available = clip_x1 - clip_x0;
    int width = text5x7_ink_width(text);
    if (width <= 0) return;

    if (width <= available) {
        int x = clip_x0 + (available - width) / 2;
        draw_text5x7_clipped(x, DASHBOARD_CLOCK_DATE_Y, text, 11, clip_x0, clip_x1);
        return;
    }

    int cycle_width = width + DASHBOARD_MARQUEE_GAP_PX;
    int x = clip_x0 - dashboard_marquee_offset(cycle_width, started_ms);
    draw_text5x7_clipped(x, DASHBOARD_CLOCK_DATE_Y, text, 11, clip_x0, clip_x1);
    x += cycle_width;
    draw_text5x7_clipped(x, DASHBOARD_CLOCK_DATE_Y, text, 11, clip_x0, clip_x1);
}

static uint32_t utf8_next_codepoint(const unsigned char **cursor) {
    const unsigned char *p = cursor ? *cursor : NULL;
    if (!p || !*p) return 0;

    uint32_t cp;
    size_t count;
    if (p[0] < 0x80) {
        cp = p[0];
        count = 1;
    } else if ((p[0] & 0xe0) == 0xc0 && p[1]) {
        cp = (uint32_t)(p[0] & 0x1f);
        count = 2;
    } else if ((p[0] & 0xf0) == 0xe0 && p[1] && p[2]) {
        cp = (uint32_t)(p[0] & 0x0f);
        count = 3;
    } else if ((p[0] & 0xf8) == 0xf0 && p[1] && p[2] && p[3]) {
        cp = (uint32_t)(p[0] & 0x07);
        count = 4;
    } else {
        *cursor = p + 1;
        return 0xfffd;
    }

    for (size_t i = 1; i < count; i++) {
        if ((p[i] & 0xc0) != 0x80) {
            *cursor = p + 1;
            return 0xfffd;
        }
        cp = (cp << 6) | (uint32_t)(p[i] & 0x3f);
    }

    if ((count == 2 && cp < 0x80) ||
        (count == 3 && cp < 0x800) ||
        (count == 4 && cp < 0x10000) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        *cursor = p + 1;
        return 0xfffd;
    }

    *cursor = p + count;
    return cp;
}


static void oled_filter_metadata_text(const char *input, char *output, size_t output_len) {
    if (!output || output_len == 0) return;
    output[0] = '\0';
    if (!input || !*input) return;

    const unsigned char *cursor = (const unsigned char *)input;
    size_t used = 0;
    int previous_was_space = 1;

    while (*cursor) {
        const unsigned char *start = cursor;
        uint32_t cp = utf8_next_codepoint(&cursor);

        /* Combining marks have no standalone OLED glyph. */
        if (cp >= 0x0300 && cp <= 0x036f) continue;

        /* Malformed or unsupported code points would render as question marks. */
        if (font5x7_glyph(cp) == font5x7_unknown) continue;

        if (cp == ' ' || cp == 0x00a0) {
            if (previous_was_space) continue;
            if (used + 1 >= output_len) break;
            output[used++] = ' ';
            previous_was_space = 1;
            continue;
        }

        size_t bytes = (size_t)(cursor - start);
        if (bytes == 0 || used + bytes >= output_len) break;
        memcpy(output + used, start, bytes);
        used += bytes;
        previous_was_space = 0;
    }

    while (used > 0 && output[used - 1] == ' ') used--;
    output[used] = '\0';
}


static void draw_pixel_digit(int x, int y, int sx, int sy, int digit, uint8_t c, int bold) {
    if (digit < 0 || digit > 9) return;
    for (int row = 0; row < 7; row++) {
        uint8_t bits = font5x7_digits[digit][row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                int bw = sx + (bold ? 1 : 0);
                oled_fill_rect(x + col * sx, y + row * sy, bw, sy, c);
            }
        }
    }
}

static void draw_pixel_colon(int x, int y, int sx, int sy, uint8_t c, int bold) {
    int dot_w = sx + (bold ? 1 : 0);
    oled_fill_rect(x, y + sy * 2, dot_w, sy, c);
    oled_fill_rect(x, y + sy * 4, dot_w, sy, c);
}

static void font_cache_close_locked(void) {
    if (g_font.face) {
        FT_Done_Face(g_font.face);
        g_font.face = NULL;
    }
    if (g_font.library) {
        FT_Done_FreeType(g_font.library);
        g_font.library = NULL;
    }
    g_font.loaded_file[0] = '\0';
    g_font.loaded_size = 0;
}

static void font_cache_reset(void) {
    pthread_mutex_lock(&g_font.lock);
    if (g_font.face) {
        FT_Done_Face(g_font.face);
        g_font.face = NULL;
    }
    g_font.loaded_file[0] = '\0';
    g_font.loaded_size = 0;
    pthread_mutex_unlock(&g_font.lock);
}

static int font_cache_ensure_locked(const char *font_file, const char *font_path, int px_size) {
    if (!font_file || !*font_file || !font_path || !*font_path) return -1;
    if (px_size < 8) px_size = 8;
    if (px_size > 72) px_size = 72;

    if (g_font.face &&
        strcmp(g_font.loaded_file, font_file) == 0 &&
        g_font.loaded_size == px_size) {
        return 0;
    }

    if (g_font.face) {
        FT_Done_Face(g_font.face);
        g_font.face = NULL;
    }

    if (!g_font.library) {
        if (FT_Init_FreeType(&g_font.library) != 0) {
            g_font.library = NULL;
            return -1;
        }
    }

    if (FT_New_Face(g_font.library, font_path, 0, &g_font.face) != 0) {
        g_font.face = NULL;
        g_font.loaded_file[0] = '\0';
        g_font.loaded_size = 0;
        return -1;
    }

    if (FT_Set_Pixel_Sizes(g_font.face, 0, (FT_UInt)px_size) != 0) {
        FT_Done_Face(g_font.face);
        g_font.face = NULL;
        g_font.loaded_file[0] = '\0';
        g_font.loaded_size = 0;
        return -1;
    }

    safe_str(g_font.loaded_file, sizeof(g_font.loaded_file), font_file);
    g_font.loaded_size = px_size;
    return 0;
}


static int clock_colon_blink_phase(void) {
    /* Simple 1-second blink: one second on, one second off. */
    time_t now = time(NULL);
    return (int)(now & 1);
}

static uint8_t clock_colon_blink_level(void) {
    /* Fully visible for one second, fully hidden for one second. */
    return clock_colon_blink_phase() ? 15 : 0;
}

/*
   Draw the main clock time on a fixed numeric grid.

   Every numeric position uses the widest visible digit in the selected font.
   Numeric ink is right-aligned inside those slots, while the colon is centred
   in its own slot. In 12-hour mode, single-digit hours use a fixed H:MM grid;
   two-digit and 24-hour times use a fixed HH:MM grid. The selected grid stays
   centred and minute changes never move its origin.
*/

struct dashboard_clock_cell_metrics {
    int digit_cell_w;
    int colon_cell_w;
    int gap;
    int min_y;
    int max_y;
    int total_w;
};

static int dashboard_load_mono_glyph_locked(FT_Face face, unsigned char ch) {
    if (!face) return -1;
    if (FT_Load_Char(face, ch, FT_LOAD_DEFAULT | FT_LOAD_TARGET_MONO) != 0) return -1;
    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO) != 0) return -1;
    return 0;
}

static int dashboard_clock_metrics_locked(FT_Face face,
                                          struct dashboard_clock_cell_metrics *metrics) {
    if (!face || !metrics) return -1;

    int max_digit_w = 0;
    int colon_w = 0;
    int min_y = 99999;
    int max_y = -99999;

    for (unsigned char ch = '0'; ch <= '9'; ch++) {
        if (dashboard_load_mono_glyph_locked(face, ch) != 0) return -1;
        FT_GlyphSlot glyph = face->glyph;
        FT_Bitmap *bitmap = &glyph->bitmap;
        if ((int)bitmap->width > max_digit_w) max_digit_w = (int)bitmap->width;
        int y0 = -glyph->bitmap_top;
        int y1 = y0 + (int)bitmap->rows;
        if (y0 < min_y) min_y = y0;
        if (y1 > max_y) max_y = y1;
    }

    if (dashboard_load_mono_glyph_locked(face, ':') != 0) return -1;
    FT_GlyphSlot colon = face->glyph;
    colon_w = (int)colon->bitmap.width;
    int colon_y0 = -colon->bitmap_top;
    int colon_y1 = colon_y0 + (int)colon->bitmap.rows;
    if (colon_y0 < min_y) min_y = colon_y0;
    if (colon_y1 > max_y) max_y = colon_y1;

    if (max_digit_w <= 0 || colon_w <= 0 || max_y <= min_y) return -1;

    metrics->digit_cell_w = max_digit_w + 2;
    metrics->colon_cell_w = colon_w + 2;
    metrics->gap = 1;
    metrics->min_y = min_y;
    metrics->max_y = max_y;
    metrics->total_w = metrics->digit_cell_w * 4 + metrics->colon_cell_w + metrics->gap * 4;
    return 0;
}

static void dashboard_draw_mono_glyph_locked(FT_Face face, unsigned char ch,
                                             int cell_x, int cell_w,
                                             int baseline_y, uint8_t level) {
    if (!face || level == 0) return;
    if (dashboard_load_mono_glyph_locked(face, ch) != 0) return;

    FT_GlyphSlot glyph = face->glyph;
    FT_Bitmap *bitmap = &glyph->bitmap;
    int gx;
    if (ch == ':')
        gx = cell_x + (cell_w - (int)bitmap->width) / 2;
    else
        gx = cell_x + cell_w - 1 - (int)bitmap->width;
    int gy = baseline_y - glyph->bitmap_top;
    int pitch = bitmap->pitch;
    int row_stride = pitch >= 0 ? pitch : -pitch;

    for (unsigned int row = 0; row < bitmap->rows; row++) {
        unsigned int source_row = pitch >= 0 ? row : bitmap->rows - 1u - row;
        const unsigned char *row_data = bitmap->buffer + source_row * (unsigned int)row_stride;
        for (unsigned int col = 0; col < bitmap->width; col++) {
            if (row_data[col >> 3] & (0x80u >> (col & 7u)))
                oled_set_px(gx + (int)col, gy + (int)row, level);
        }
    }
}

static int draw_dashboard_time_ttf_binary(const char *font_file, int upper_size,
                                           const char *time_text,
                                           int region_x0, int region_y0,
                                           int region_x1, int region_y1,
                                           int show_leading_zero,
                                           uint8_t colon_level) {
    if (!font_file || !*font_file || !time_text || !*time_text) return -1;

    int hour = 0;
    int minute = 0;
    if (sscanf(time_text, "%d:%d", &hour, &minute) != 2) return -1;
    hour = clamp_int(hour, 0, 99);
    minute = clamp_int(minute, 0, 59);

    char font_path[512];
    make_font_path(font_file, font_path, sizeof(font_path));
    upper_size = clamp_int(upper_size, 18, 72);

    pthread_mutex_lock(&g_font.lock);

    int selected_size = 0;
    int region_w = region_x1 - region_x0 + 1;
    int region_h = region_y1 - region_y0 + 1;
    struct dashboard_clock_cell_metrics metrics;
    memset(&metrics, 0, sizeof(metrics));

    int cached = strcmp(g_dashboard_fit_font, font_file) == 0 &&
                 g_dashboard_fit_upper_size == upper_size &&
                 g_dashboard_fit_selected_size >= 18;

    if (cached) {
        selected_size = g_dashboard_fit_selected_size;
        if (font_cache_ensure_locked(font_file, font_path, selected_size) != 0 || !g_font.face ||
            dashboard_clock_metrics_locked(g_font.face, &metrics) != 0) {
            pthread_mutex_unlock(&g_font.lock);
            return -1;
        }
    } else {
        if (font_cache_ensure_locked(font_file, font_path, upper_size) != 0 || !g_font.face) {
            pthread_mutex_unlock(&g_font.lock);
            return -1;
        }

        for (int size = upper_size; size >= 18; size--) {
            if (FT_Set_Pixel_Sizes(g_font.face, 0, (FT_UInt)size) != 0) continue;
            if (dashboard_clock_metrics_locked(g_font.face, &metrics) != 0) continue;
            int height = metrics.max_y - metrics.min_y;
            if (metrics.total_w <= region_w && height <= region_h) {
                selected_size = size;
                break;
            }
        }

        if (selected_size == 0) {
            pthread_mutex_unlock(&g_font.lock);
            return -1;
        }

        g_font.loaded_size = selected_size;
        safe_str(g_dashboard_fit_font, sizeof(g_dashboard_fit_font), font_file);
        g_dashboard_fit_upper_size = upper_size;
        g_dashboard_fit_selected_size = selected_size;
    }

    int height = metrics.max_y - metrics.min_y;
    int baseline_y = region_y0 + (region_h - height) / 2 - metrics.min_y;

    unsigned char digits[4] = {
        (unsigned char)('0' + ((hour / 10) % 10)),
        (unsigned char)('0' + (hour % 10)),
        (unsigned char)('0' + (minute / 10)),
        (unsigned char)('0' + (minute % 10))
    };
    int visible_hour_digits = (show_leading_zero || hour >= 10) ? 2 : 1;
    int grid_w = metrics.digit_cell_w * (visible_hour_digits + 2) +
                 metrics.colon_cell_w + metrics.gap * (visible_hour_digits + 2);
    int x = dashboard_clock_center_x() - (grid_w / 2);

    if (visible_hour_digits == 2) {
        dashboard_draw_mono_glyph_locked(g_font.face, digits[0], x,
                                         metrics.digit_cell_w, baseline_y, 15);
        x += metrics.digit_cell_w + metrics.gap;
    }

    dashboard_draw_mono_glyph_locked(g_font.face, digits[1], x,
                                     metrics.digit_cell_w, baseline_y, 15);
    x += metrics.digit_cell_w + metrics.gap;

    dashboard_draw_mono_glyph_locked(g_font.face, ':', x,
                                     metrics.colon_cell_w, baseline_y, colon_level);
    x += metrics.colon_cell_w + metrics.gap;

    dashboard_draw_mono_glyph_locked(g_font.face, digits[2], x,
                                     metrics.digit_cell_w, baseline_y, 15);
    x += metrics.digit_cell_w + metrics.gap;

    dashboard_draw_mono_glyph_locked(g_font.face, digits[3], x,
                                     metrics.digit_cell_w, baseline_y, 15);

    pthread_mutex_unlock(&g_font.lock);
    return 0;
}


struct dashboard_text_metrics {
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int advance_x;
};

static int dashboard_text_metrics_locked(FT_Face face, const char *text,
                                         struct dashboard_text_metrics *metrics) {
    if (!face || !text || !*text || !metrics) return -1;

    int pen_x = 0;
    int min_x = 99999;
    int max_x = -99999;
    int min_y = 99999;
    int max_y = -99999;

    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (dashboard_load_mono_glyph_locked(face, *p) != 0) return -1;
        FT_GlyphSlot glyph = face->glyph;
        FT_Bitmap *bitmap = &glyph->bitmap;
        int glyph_x0 = pen_x + glyph->bitmap_left;
        int glyph_x1 = glyph_x0 + (int)bitmap->width;
        int glyph_y0 = -glyph->bitmap_top;
        int glyph_y1 = glyph_y0 + (int)bitmap->rows;
        if (glyph_x0 < min_x) min_x = glyph_x0;
        if (glyph_x1 > max_x) max_x = glyph_x1;
        if (glyph_y0 < min_y) min_y = glyph_y0;
        if (glyph_y1 > max_y) max_y = glyph_y1;

        int advance = (int)(glyph->advance.x >> 6);
        if (advance <= 0) advance = (int)bitmap->width + 1;
        pen_x += advance;
    }

    if (max_x <= min_x || max_y <= min_y) return -1;
    metrics->min_x = min_x;
    metrics->max_x = max_x;
    metrics->min_y = min_y;
    metrics->max_y = max_y;
    metrics->advance_x = pen_x;
    return 0;
}

static void dashboard_draw_text_glyph_locked(FT_Face face, unsigned char ch,
                                             int pen_x, int baseline_y,
                                             uint8_t level) {
    if (!face || level == 0 || dashboard_load_mono_glyph_locked(face, ch) != 0) return;

    FT_GlyphSlot glyph = face->glyph;
    FT_Bitmap *bitmap = &glyph->bitmap;
    int gx = pen_x + glyph->bitmap_left;
    int gy = baseline_y - glyph->bitmap_top;
    int pitch = bitmap->pitch;
    int row_stride = pitch >= 0 ? pitch : -pitch;

    for (unsigned int row = 0; row < bitmap->rows; row++) {
        unsigned int source_row = pitch >= 0 ? row : bitmap->rows - 1u - row;
        const unsigned char *row_data =
            bitmap->buffer + source_row * (unsigned int)row_stride;
        for (unsigned int col = 0; col < bitmap->width; col++) {
            if (row_data[col >> 3] & (0x80u >> (col & 7u)))
                oled_set_px(gx + (int)col, gy + (int)row, level);
        }
    }
}

static int draw_room_temperature_ttf_binary(const char *font_file, int upper_size,
                                             const char *temperature_text,
                                             int region_x0, int region_y0,
                                             int region_x1, int region_y1,
                                             uint8_t level) {
    if (!font_file || !*font_file || !temperature_text || !*temperature_text)
        return -1;

    char font_path[512];
    make_font_path(font_file, font_path, sizeof(font_path));
    upper_size = clamp_int(upper_size, 14, 34);

    pthread_mutex_lock(&g_font.lock);

    int selected_size = 0;
    struct dashboard_text_metrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    const int region_w = region_x1 - region_x0 + 1;
    const int region_h = region_y1 - region_y0 + 1;
    const int degree_reserve = 4;

    int cached = strcmp(g_room_fit_font, font_file) == 0 &&
                 g_room_fit_upper_size == upper_size &&
                 g_room_fit_selected_size >= 14;

    if (cached) {
        selected_size = g_room_fit_selected_size;
        if (font_cache_ensure_locked(font_file, font_path, selected_size) != 0 ||
            !g_font.face ||
            dashboard_text_metrics_locked(g_font.face, temperature_text, &metrics) != 0) {
            pthread_mutex_unlock(&g_font.lock);
            return -1;
        }
    } else {
        if (font_cache_ensure_locked(font_file, font_path, upper_size) != 0 || !g_font.face) {
            pthread_mutex_unlock(&g_font.lock);
            return -1;
        }

        struct dashboard_text_metrics worst_case;
        memset(&worst_case, 0, sizeof(worst_case));
        for (int size = upper_size; size >= 14; size--) {
            if (FT_Set_Pixel_Sizes(g_font.face, 0, (FT_UInt)size) != 0) continue;
            if (dashboard_text_metrics_locked(g_font.face, "-99.9", &worst_case) != 0)
                continue;
            int worst_w = worst_case.max_x - worst_case.min_x;
            int worst_h = worst_case.max_y - worst_case.min_y;
            if (worst_w + degree_reserve <= region_w && worst_h <= region_h) {
                selected_size = size;
                break;
            }
        }

        if (selected_size == 0 ||
            FT_Set_Pixel_Sizes(g_font.face, 0, (FT_UInt)selected_size) != 0 ||
            dashboard_text_metrics_locked(g_font.face, temperature_text, &metrics) != 0) {
            pthread_mutex_unlock(&g_font.lock);
            return -1;
        }

        g_font.loaded_size = selected_size;
        safe_str(g_room_fit_font, sizeof(g_room_fit_font), font_file);
        g_room_fit_upper_size = upper_size;
        g_room_fit_selected_size = selected_size;
    }

    int text_w = metrics.max_x - metrics.min_x;
    int text_h = metrics.max_y - metrics.min_y;
    int total_w = text_w + degree_reserve;
    int text_left = region_x0 + (region_w - total_w) / 2;
    int pen_x = text_left - metrics.min_x;
    int baseline_y = region_y0 + (region_h - text_h) / 2 - metrics.min_y;

    for (const unsigned char *ch = (const unsigned char *)temperature_text; *ch; ch++) {
        if (dashboard_load_mono_glyph_locked(g_font.face, *ch) != 0) continue;
        int advance = (int)(g_font.face->glyph->advance.x >> 6);
        if (advance <= 0) advance = (int)g_font.face->glyph->bitmap.width + 1;
        dashboard_draw_text_glyph_locked(g_font.face, *ch, pen_x, baseline_y, level);
        pen_x += advance;
    }

    int degree_x = text_left + text_w + 1;
    int degree_y = baseline_y + metrics.min_y + 1;
    oled_set_px(degree_x + 1, degree_y, level);
    oled_set_px(degree_x, degree_y + 1, level);
    oled_set_px(degree_x + 2, degree_y + 1, level);
    oled_set_px(degree_x + 1, degree_y + 2, level);

    pthread_mutex_unlock(&g_font.lock);
    return 0;
}

static void draw_weather_compact_text_scaled_centered(
    int x0,
    int x1,
    int y,
    const char *text,
    int scale,
    uint8_t level
) {
    if (!text || scale < 1) return;

    int unscaled_width = weather_compact_text_width(text);
    int width = unscaled_width * scale;
    int x = x0 + ((x1 - x0 + 1 - width) / 2);

    for (const char *p = text; *p; p++) {
        uint8_t rows[5];
        int glyph_width = weather_compact_glyph(*p, rows);
        if (glyph_width <= 0) continue;
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < glyph_width; col++) {
                if (rows[row] & (1u << (glyph_width - 1 - col)))
                    oled_fill_rect(x + col * scale, y + row * scale,
                                   scale, scale, level);
            }
        }
        x += (glyph_width + 1) * scale;
    }
}

static void draw_room_temperature_panel(
    int x0,
    int x1,
    const char *font_file,
    int upper_font_size,
    const struct room_sensor_snapshot *sensor
) {
    char temperature_text[16];
    char fallback_text[16];
    char humidity_text[16];

    int has_reading = sensor &&
        (sensor->status == ROOM_SENSOR_ACTIVE || sensor->status == ROOM_SENSOR_STALE);
    uint8_t temperature_level =
        sensor && sensor->status == ROOM_SENSOR_STALE ? 9 : 15;
    uint8_t humidity_level =
        sensor && sensor->status == ROOM_SENSOR_STALE ? 6 : 10;

    if (has_reading) {
        double temperature = sensor->temperature_c;
        if (temperature < -99.9) temperature = -99.9;
        if (temperature > 99.9) temperature = 99.9;
        int humidity = clamp_int((int)lround(sensor->humidity_percent), 0, 100);
        snprintf(temperature_text, sizeof(temperature_text), "%.1f", temperature);
        snprintf(fallback_text, sizeof(fallback_text), "%.1f^", temperature);
        snprintf(humidity_text, sizeof(humidity_text), "%d%%", humidity);
    } else {
        safe_str(temperature_text, sizeof(temperature_text), "--.-");
        safe_str(fallback_text, sizeof(fallback_text), "--.-^");
        safe_str(humidity_text, sizeof(humidity_text), "--%");
        temperature_level = 8;
        humidity_level = 6;
    }

    int used_ttf = font_file && *font_file &&
        draw_room_temperature_ttf_binary(
            font_file,
            upper_font_size,
            temperature_text,
            x0 + 2,
            13,
            x1 - 2,
            43,
            temperature_level
        ) == 0;

    if (!used_ttf)
        draw_weather_compact_text_scaled_centered(
            x0 + 1, x1 - 1, 23, fallback_text, 2, temperature_level);

    draw_weather_compact_text_centered(
        x0 + 1, x1 - 1, WEATHER_PANEL_LOWER_ROW_Y,
        humidity_text, humidity_level);
}


static void draw_dashboard_pixel_digit_right_aligned(int cell_x, int cell_w,
                                                       int y, int sx, int sy,
                                                       int digit, uint8_t level) {
    if (digit < 0 || digit > 9) return;

    int rightmost_column = -1;
    for (int row = 0; row < 7; row++) {
        uint8_t bits = font5x7_digits[digit][row];
        for (int col = 0; col < 5; col++) {
            if ((bits & (1 << (4 - col))) && col > rightmost_column)
                rightmost_column = col;
        }
    }
    if (rightmost_column < 0) return;

    int x = cell_x + cell_w - (rightmost_column + 1) * sx;
    draw_pixel_digit(x, y, sx, sy, digit, level, 0);
}

static void draw_dashboard_time_pixel_fallback(int hour, int minute,
                                                int show_leading_zero,
                                                uint8_t colon_level) {
    hour = clamp_int(hour, 0, 99);
    minute = clamp_int(minute, 0, 59);

    const int region_y0 = DASHBOARD_CLOCK_TIME_Y0;
    const int region_y1 = DASHBOARD_CLOCK_TIME_Y1;
    const int sx = 3;
    const int sy = 6;
    const int digit_cell_w = 5 * sx;
    const int colon_cell_w = sx;
    const int gap = 2;
    const int glyph_h = 7 * sy;
    int visible_hour_digits = (show_leading_zero || hour >= 10) ? 2 : 1;
    int grid_w = digit_cell_w * (visible_hour_digits + 2) +
                 colon_cell_w + gap * (visible_hour_digits + 2);

    int x = dashboard_clock_center_x() - (grid_w / 2);
    int y = region_y0 + ((region_y1 - region_y0 + 1) - glyph_h) / 2;
    int digits[4] = {hour / 10, hour % 10, minute / 10, minute % 10};

    if (visible_hour_digits == 2) {
        draw_dashboard_pixel_digit_right_aligned(x, digit_cell_w, y, sx, sy, digits[0], 15);
        x += digit_cell_w + gap;
    }
    draw_dashboard_pixel_digit_right_aligned(x, digit_cell_w, y, sx, sy, digits[1], 15);
    x += digit_cell_w + gap;
    draw_pixel_colon(x, y, sx, sy, colon_level, 0);
    x += colon_cell_w + gap;
    draw_dashboard_pixel_digit_right_aligned(x, digit_cell_w, y, sx, sy, digits[2], 15);
    x += digit_cell_w + gap;
    draw_dashboard_pixel_digit_right_aligned(x, digit_cell_w, y, sx, sy, digits[3], 15);
}



/*
 * Centre the clock from the pixels that were actually rendered.
 *
 * Font advance widths, side bearings, and narrow digits can make a nominally
 * centred cell grid look offset. This pass finds the visible horizontal bounds
 * and translates the complete clock as one image. It therefore works for every
 * FreeType font and for the built-in pixel fallback without font-specific
 * offsets. The colon blink cannot change the result because the outer digits
 * define the horizontal bounds.
 */
static int dashboard_center_rendered_clock_x(int x0, int y0, int x1, int y1) {
    x0 = clamp_int(x0, 0, OLED_W - 1);
    x1 = clamp_int(x1, 0, OLED_W - 1);
    y0 = clamp_int(y0, 0, OLED_H - 1);
    y1 = clamp_int(y1, 0, OLED_H - 1);
    if (x1 < x0 || y1 < y0) return 0;

    int ink_x0 = x1 + 1;
    int ink_x1 = x0 - 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (oled_get_px(x, y) == 0) continue;
            if (x < ink_x0) ink_x0 = x;
            if (x > ink_x1) ink_x1 = x;
        }
    }
    if (ink_x1 < ink_x0) return 0;

    const int target_center2 = x0 + x1;
    const int ink_center2 = ink_x0 + ink_x1;
    const int delta2 = target_center2 - ink_center2;
    int shift_x = delta2 >= 0 ? (delta2 + 1) / 2 : (delta2 - 1) / 2;
    shift_x = clamp_int(shift_x, x0 - ink_x0, x1 - ink_x1);
    if (shift_x == 0) return 0;

    uint8_t row[OLED_W];
    const int width = x1 - x0 + 1;
    for (int y = y0; y <= y1; y++) {
        for (int i = 0; i < width; i++) row[i] = oled_get_px(x0 + i, y);
        for (int x = x0; x <= x1; x++) oled_set_px(x, y, 0);
        for (int i = 0; i < width; i++) {
            int destination_x = x0 + i + shift_x;
            if (row[i] != 0 && destination_x >= x0 && destination_x <= x1)
                oled_set_px(destination_x, y, row[i]);
        }
    }
    return shift_x;
}

static void dashboard_audio_display(char *out, size_t out_len,
                                    const char *title, const char *artist,
                                    const char *file) {
    if (!out || out_len == 0) return;
    out[0] = '\0';

    /* Filter local OLED copies only; API and web metadata remain unmodified. */
    char oled_title[MP_ID3_TEXT_MAX];
    char oled_artist[MP_ID3_TEXT_MAX];
    char oled_file[MUSIC_FILE_MAX];
    oled_filter_metadata_text(title, oled_title, sizeof(oled_title));
    oled_filter_metadata_text(artist, oled_artist, sizeof(oled_artist));
    oled_filter_metadata_text(file, oled_file, sizeof(oled_file));

    if (oled_title[0] && oled_artist[0])
        snprintf(out, out_len, "%s - %s", oled_title, oled_artist);
    else if (oled_title[0])
        safe_str(out, out_len, oled_title);
    else if (oled_artist[0])
        safe_str(out, out_len, oled_artist);
    else if (oled_file[0])
        safe_str(out, out_len, oled_file);
    else
        safe_str(out, out_len, "NOW PLAYING");
}

static int dashboard_footer_text_is_marquee(const char *text) {
    int available = DASHBOARD_FOOTER_X1 - DASHBOARD_FOOTER_X0 + 1;
    return text5x7_ink_width(text ? text : "") > available;
}

static uint64_t weather_warning_display_duration_ms(const char *text) {
    int width = text5x7_ink_width(text ? text : "");
    int available = DASHBOARD_FOOTER_X1 - DASHBOARD_FOOTER_X0 + 1;
    if (width <= available) return WEATHER_WARNING_MIN_DISPLAY_MS;

    /* One exact marquee cycle. Never rotate or restart midway through a pass. */
    uint64_t cycle_pixels = (uint64_t)width + DASHBOARD_MARQUEE_GAP_PX;
    return (cycle_pixels * 1000u + DASHBOARD_MARQUEE_SPEED_PX_PER_SEC - 1u) /
           DASHBOARD_MARQUEE_SPEED_PX_PER_SEC;
}

static int weather_warning_next_text(
    const struct weather_dashboard_snapshot *weather,
    const char *current_text,
    char *out,
    size_t out_len
) {
    if (!out || out_len == 0) return 0;
    out[0] = '\0';
    if (!weather || weather->warning_count <= 0) return 0;

    int next_index = 0;
    if (current_text && current_text[0]) {
        for (int i = 0; i < weather->warning_count; i++) {
            if (strcmp(weather->warning_descriptions[i], current_text) == 0) {
                next_index = (i + 1) % weather->warning_count;
                break;
            }
        }
    }

    safe_str(out, out_len, weather->warning_descriptions[next_index]);
    return out[0] != '\0';
}

static int dashboard_weather_warning_state(
    const struct weather_dashboard_snapshot *weather,
    char *text,
    size_t text_len,
    uint64_t *started_ms
) {
    if (!text || text_len == 0) return 0;

    uint64_t now_ms = monotonic_millis();
    if (!g_weather_warning_display.text[0]) {
        if (!weather_warning_next_text(
                weather, "", g_weather_warning_display.text,
                sizeof(g_weather_warning_display.text))) {
            return 0;
        }
        g_weather_warning_display.started_ms = now_ms;
        g_weather_warning_display.refresh_needed = 1;
    } else {
        uint64_t duration_ms = weather_warning_display_duration_ms(
            g_weather_warning_display.text);
        uint64_t elapsed_ms = now_ms >= g_weather_warning_display.started_ms
            ? now_ms - g_weather_warning_display.started_ms : 0;

        if (elapsed_ms >= duration_ms) {
            char next_text[MP_WEATHER_WARNING_TEXT_MAX];
            if (!weather_warning_next_text(
                    weather, g_weather_warning_display.text,
                    next_text, sizeof(next_text))) {
                g_weather_warning_display.text[0] = '\0';
                g_weather_warning_display.started_ms = 0;
                g_weather_warning_display.refresh_needed = 1;
                return 0;
            }

            safe_str(g_weather_warning_display.text,
                     sizeof(g_weather_warning_display.text), next_text);
            g_weather_warning_display.started_ms = now_ms;
            g_weather_warning_display.refresh_needed = 1;
        }
    }

    safe_str(text, text_len, g_weather_warning_display.text);
    if (started_ms) *started_ms = g_weather_warning_display.started_ms;
    return text[0] != '\0';
}

static int dashboard_footer_state(char *text, size_t text_len,
                                  uint64_t *started_ms, int *is_marquee) {
    if (!text || text_len == 0) return 0;
    text[0] = '\0';
    if (started_ms) *started_ms = 0;
    if (is_marquee) *is_marquee = 0;

    int audio_playing;
    char audio_file[MUSIC_FILE_MAX];
    char audio_title[MP_ID3_TEXT_MAX];
    char audio_artist[MP_ID3_TEXT_MAX];
    uint64_t audio_started_ms;
    pthread_mutex_lock(&g_state.lock);
    audio_playing = g_state.audio_playing;
    safe_str(audio_file, sizeof(audio_file), g_state.audio_file);
    safe_str(audio_title, sizeof(audio_title), g_state.audio_title);
    safe_str(audio_artist, sizeof(audio_artist), g_state.audio_artist);
    audio_started_ms = g_state.audio_scroll_started_ms;
    pthread_mutex_unlock(&g_state.lock);

    if (audio_playing) {
        dashboard_audio_display(text, text_len, audio_title, audio_artist, audio_file);
        if (started_ms) *started_ms = audio_started_ms;
    } else {
        struct weather_dashboard_snapshot weather;
        memset(&weather, 0, sizeof(weather));
        weather_snapshot(&weather);
        (void)dashboard_weather_warning_state(
            &weather, text, text_len, started_ms);
    }

    if (!text[0]) return 0;
    if (is_marquee) *is_marquee = dashboard_footer_text_is_marquee(text);
    return 1;
}

static void draw_dashboard_footer(const struct tm *tmv) {
    char footer_text[MP_ID3_TEXT_MAX * 2 + 4];
    uint64_t started_ms = 0;
    if (dashboard_footer_state(footer_text, sizeof(footer_text), &started_ms, NULL)) {
        draw_dashboard_marquee_line(footer_text, started_ms);
        return;
    }

    static const char *day_names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *month_names[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    int wday = tmv && tmv->tm_wday >= 0 && tmv->tm_wday <= 6 ? tmv->tm_wday : 0;
    int month = tmv && tmv->tm_mon >= 0 && tmv->tm_mon <= 11 ? tmv->tm_mon : 0;
    int day = tmv ? tmv->tm_mday : 1;
    char date_text[24];
    snprintf(date_text, sizeof(date_text), "%s, %s %d", day_names[wday], month_names[month], day);
    draw_text5x7_centered(DASHBOARD_FOOTER_X0, DASHBOARD_FOOTER_X1,
                          DASHBOARD_CLOCK_DATE_Y, date_text, 11);
}

static int dashboard_footer_refresh_active(void) {
    char footer_text[MP_ID3_TEXT_MAX * 2 + 4];
    uint64_t started_ms = 0;
    int marquee = 0;
    int has_footer = dashboard_footer_state(
        footer_text, sizeof(footer_text), &started_ms, &marquee);
    int refresh_needed = g_weather_warning_display.refresh_needed;
    g_weather_warning_display.refresh_needed = 0;
    return refresh_needed || (has_footer && marquee);
}

static void refresh_dashboard_footer(void) {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    oled_fill_rect(WEATHER_LEFT_X0, DASHBOARD_CLOCK_DATE_Y,
                   WEATHER_LEFT_X1 - WEATHER_LEFT_X0, 7, 0);
    draw_dashboard_footer(&tmv);
    (void)oled_flush_region_bytes(0, (WEATHER_LEFT_X1 - 1) / 2,
                                  DASHBOARD_CLOCK_DATE_Y,
                                  DASHBOARD_CLOCK_DATE_Y + 6);
}

static int collect_music_files(char files[][ASSET_LIST_NAME_MAX], int max_files) {
    return scan_asset_files(MUSIC_DIR, ASSET_LIST_MUSIC_MP3, files, max_files);
}

static void draw_weather_dashboard_screen(void) {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    int clock_24h_mode;
    int upper_font_size;
    int alarm_on = 0;
    char font_file[128];
    char inside_font_file[128];
    pthread_mutex_lock(&g_state.lock);
    clock_24h_mode = g_state.clock_24h_mode;
    upper_font_size = clamp_int(g_state.oled_font_size, 18, 72);
    safe_str(font_file, sizeof(font_file), g_state.oled_font_file);
    safe_str(inside_font_file, sizeof(inside_font_file), g_state.inside_font_file);
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (g_state.alarms[i].enabled && g_state.alarms[i].weekdays) {
            alarm_on = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_state.lock);

    struct weather_dashboard_snapshot weather;
    memset(&weather, 0, sizeof(weather));
    weather_snapshot(&weather);

    struct room_sensor_snapshot room;
    memset(&room, 0, sizeof(room));
    room_sensor_snapshot(&room);

    int hour = tmv.tm_hour;
    if (!clock_24h_mode) {
        if (hour == 0) hour = 12;
        else if (hour > 12) hour -= 12;
    }

    char time_text[32];
    if (clock_24h_mode) snprintf(time_text, sizeof(time_text), "%02d:%02d", hour, tmv.tm_min);
    else snprintf(time_text, sizeof(time_text), "%d:%02d", hour, tmv.tm_min);

    oled_clear_fb(0);

    for (size_t i = 0; i < sizeof(weather_separators) / sizeof(weather_separators[0]); i++)
        oled_draw_line(weather_separators[i], 3, weather_separators[i], 60, 7);

    char header_label[96];
    make_weather_header_label(weather.location, alarm_on, header_label, sizeof(header_label));

    uint8_t colon_level = clock_colon_blink_level();
    int used_ttf = 0;
    if (font_file[0]) {
        used_ttf = draw_dashboard_time_ttf_binary(
            font_file,
            upper_font_size,
            time_text,
            WEATHER_LEFT_X0 + 2,
            DASHBOARD_CLOCK_TIME_Y0,
            WEATHER_LEFT_X1 - 2,
            DASHBOARD_CLOCK_TIME_Y1,
            clock_24h_mode,
            colon_level
        ) == 0;
    }
    if (!used_ttf)
        draw_dashboard_time_pixel_fallback(hour, tmv.tm_min, clock_24h_mode, colon_level);

    (void)dashboard_center_rendered_clock_x(
        DASHBOARD_CLOCK_CONTENT_X0,
        DASHBOARD_CLOCK_TIME_Y0,
        DASHBOARD_CLOCK_CONTENT_X1,
        DASHBOARD_CLOCK_TIME_Y1);

    if (header_label[0]) {
        int header_x = weather_compact_text_optical_x(
            header_label, dashboard_clock_center_x());
        draw_weather_compact_text(header_x, DASHBOARD_LOCATION_Y, header_label, 11);
    }

    draw_dashboard_seconds_position_line(tmv.tm_sec);

    draw_dashboard_footer(&tmv);

    for (int i = 0; i < MP_WEATHER_FORECAST_SLOTS; i++) {
        int x0 = weather_panel_x0[i];
        int x1 = weather_panel_x1[i];
        int cx = (x0 + x1) / 2;
        int is_room = weather.slots[i].kind == MP_WEATHER_SLOT_ROOM;
        int is_today = weather.slots[i].kind == MP_WEATHER_SLOT_TODAY;
        uint8_t label_level = is_room ? 15 : 11;
        uint8_t temperature_level = is_room ? 15 : 13;

        char label[8];
        if (is_room) {
            safe_str(label, sizeof(label), "INSIDE");
        } else {
            safe_str(label, sizeof(label), weather.slots[i].label);
            if (!label[0])
                safe_str(label, sizeof(label),
                         mp_weather_slot_default_label(weather.slots[i].kind));
        }
        for (char *p = label; *p; p++) *p = (char)toupper((unsigned char)*p);
        draw_text5x7_centered(x0 + 2, x1 - 2, 3, label, label_level);

        if (is_room) {
            draw_room_temperature_panel(
                x0,
                x1,
                inside_font_file[0] ? inside_font_file : font_file,
                upper_font_size,
                &room
            );
        } else if (is_today) {
            draw_weather_today_panel(x0, x1, &weather.slots[i]);
        } else {
            if (draw_weather_icon_asset(i, cx, WEATHER_ICON_CENTER_Y) != 0)
                draw_weather_icon_missing(cx, WEATHER_ICON_CENTER_Y);
            draw_weather_temperature_precip_centered(
                x0 + 1,
                x1 - 1,
                WEATHER_PANEL_LOWER_ROW_Y,
                weather.slots[i].temperature_c,
                weather.slots[i].temperature_available,
                weather.slots[i].precipitation_probability_percent,
                temperature_level
            );
            if (weather.slots[i].date_label[0]) {
                int date_width = weather_compact_text_width(weather.slots[i].date_label);
                int date_x = x0 + ((x1 - x0 + 1 - date_width) / 2);
                draw_weather_compact_text(date_x, 56, weather.slots[i].date_label, 10);
            }
        }
    }


    oled_flush_full();
}


struct audio_play_request {
    char path[512];
    char file[MUSIC_FILE_MAX];
    int start_volume;
    int end_volume;
    int use_ramp;
};

static int choose_random_music_file(char *out, size_t out_len) {
    if (!out || out_len == 0) return -1;
    out[0] = '\0';

    char files[ASSET_LIST_MAX_FILES][ASSET_LIST_NAME_MAX];
    int count = collect_music_files(files, ASSET_LIST_MAX_FILES);
    if (count <= 0) return -1;

    safe_str(out, out_len, files[rand() % count]);
    return 0;
}

static int audio_should_stop(void) {
    int stop;
    uint64_t now_ms = monotonic_millis();
    pthread_mutex_lock(&g_audio.lock);
    if (!g_audio.stop_requested && g_audio.alarm_mode && g_audio.alarm_deadline_ms > 0 &&
        now_ms >= g_audio.alarm_deadline_ms) {
        g_audio.stop_requested = 1;
        g_audio.timed_out = 1;
    }
    stop = g_audio.stop_requested;
    pthread_mutex_unlock(&g_audio.lock);
    return stop;
}

static void alarm_volume_state_set(int active, int volume_percent) {
    static int last_active = -1;
    static int last_volume = -999;

    volume_percent = clamp_int(volume_percent, 0, 100);

    pthread_mutex_lock(&g_state.lock);
    g_state.alarm_active = active ? 1 : 0;
    g_state.alarm_volume_percent = active ? volume_percent : 0;

    if (g_state.display_mode == 0 &&
        (last_active != g_state.alarm_active || last_volume != g_state.alarm_volume_percent)) {
        g_state.display_dirty = 1;
    }

    last_active = g_state.alarm_active;
    last_volume = g_state.alarm_volume_percent;
    pthread_mutex_unlock(&g_state.lock);
}

static void audio_scale_s16(unsigned char *buf, size_t bytes, int volume_percent) {
    volume_percent = clamp_int(volume_percent, 0, 100);
    int gain_q15 = (volume_percent * 32768) / 100;
    int16_t *samples = (int16_t *)buf;
    size_t count = bytes / sizeof(int16_t);

    for (size_t i = 0; i < count; i++) {
        int32_t v = ((int32_t)samples[i] * gain_q15) >> 15;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        samples[i] = (int16_t)v;
    }
}

static int audio_write_pcm(snd_pcm_t *pcm, unsigned char *buf, size_t bytes, int channels) {
    const int frame_bytes = channels * (int)sizeof(int16_t);
    if (frame_bytes <= 0) return -1;

    size_t offset = 0;
    snd_pcm_sframes_t frames_left = (snd_pcm_sframes_t)(bytes / (size_t)frame_bytes);

    while (frames_left > 0 && !audio_should_stop()) {
        snd_pcm_sframes_t written = snd_pcm_writei(pcm, buf + offset, frames_left);
        if (written == -EPIPE) {
            snd_pcm_prepare(pcm);
            continue;
        }
        if (written < 0) {
            written = snd_pcm_recover(pcm, (int)written, 1);
            if (written < 0) return -1;
            continue;
        }
        if (written == 0) continue;
        frames_left -= written;
        offset += (size_t)written * (size_t)frame_bytes;
    }
    return 0;
}

static void clear_audio_metadata_locked(void);

static void *audio_thread_main(void *arg) {
    struct audio_play_request *req = (struct audio_play_request *)arg;
    mpg123_handle *mh = NULL;
    snd_pcm_t *pcm = NULL;
    unsigned char *buf = NULL;
    int err = MPG123_OK;
    long rate = 0;
    int channels = 0;
    int enc = 0;
    off_t total_samples = 0;
    off_t played_samples = 0;
    double ramp_seconds = 0.0;
    struct timespec ramp_start_ts;
    memset(&ramp_start_ts, 0, sizeof(ramp_start_ts));

    mh = mpg123_new(NULL, &err);
    if (!mh) goto done;

    mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_QUIET, 0.0);

    if (mpg123_open(mh, req->path) != MPG123_OK) goto done;
    if (mpg123_getformat(mh, &rate, &channels, &enc) != MPG123_OK) goto done;
    if (channels < 1 || channels > 2 || rate <= 0) goto done;

    mpg123_format_none(mh);
    mpg123_format(mh, rate, channels, MPG123_ENC_SIGNED_16);

    /*
       Scan once so VBR files get a useful sample length for the alarm ramp.
       We compute seconds from decoded PCM frames, then ramp by real playback time.
       That is more reliable than tying volume changes only to decoder position.
    */
    if (req->use_ramp) {
        if (mpg123_scan(mh) == MPG123_OK) {
            total_samples = mpg123_length(mh);
            mpg123_seek(mh, 0, SEEK_SET);
        } else {
            total_samples = mpg123_length(mh);
        }
        if (total_samples < 0) total_samples = 0;
        if (total_samples > 0 && rate > 0) {
            ramp_seconds = (double)total_samples / (double)rate;
        }
        if (ramp_seconds < 1.0) ramp_seconds = 60.0;
        clock_gettime(CLOCK_MONOTONIC, &ramp_start_ts);
        alarm_volume_state_set(1, req->start_volume);
        fprintf(stderr,
                "alarm ramp: file=%s start=%d end=%d duration=%.1fs alarm-volume-overrides-global\n",
                req->file,
                req->start_volume,
                req->end_volume,
                ramp_seconds);
    } else {
        total_samples = mpg123_length(mh);
        if (total_samples < 0) total_samples = 0;
    }

    if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) goto done;
    if (snd_pcm_set_params(pcm,
                           SND_PCM_FORMAT_S16_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED,
                           (unsigned int)channels,
                           (unsigned int)rate,
                           1,
                           300000) < 0) {
        goto done;
    }

    const size_t buf_size = 8192;
    buf = malloc(buf_size);
    if (!buf) goto done;

    if (req->use_ramp) {
        pthread_mutex_lock(&g_state.lock);
        g_state.last_successful_alarm = (long long)time(NULL);
        pthread_mutex_unlock(&g_state.lock);
        save_config();
        app_log("alarm", "Alarm audio started: %s; repeats until dismissed or the 30-minute safety limit", req->file);
    }

    while (!audio_should_stop()) {
        size_t done_bytes = 0;
        int r = mpg123_read(mh, buf, buf_size, &done_bytes);
        if (done_bytes > 0) {
            int frame_bytes = channels * (int)sizeof(int16_t);
            off_t frames = (off_t)(done_bytes / (size_t)frame_bytes);
            int volume = req->start_volume;

            if (req->use_ramp && ramp_seconds > 0.0) {
                struct timespec now_ts;
                clock_gettime(CLOCK_MONOTONIC, &now_ts);
                double elapsed = (double)(now_ts.tv_sec - ramp_start_ts.tv_sec) +
                                 ((double)(now_ts.tv_nsec - ramp_start_ts.tv_nsec) / 1000000000.0);
                if (elapsed < 0.0) elapsed = 0.0;
                double t = elapsed / ramp_seconds;
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
                volume = req->start_volume + (int)((double)(req->end_volume - req->start_volume) * t + 0.5);
            } else if (req->use_ramp && total_samples > 0) {
                off_t pos = played_samples;
                if (pos < 0) pos = 0;
                if (pos > total_samples) pos = total_samples;
                volume = req->start_volume + (int)(((long long)(req->end_volume - req->start_volume) * (long long)pos) / (long long)total_samples);
            }

            if (req->use_ramp) alarm_volume_state_set(1, volume);
            audio_scale_s16(buf, done_bytes, volume);
            if (audio_write_pcm(pcm, buf, done_bytes, channels) != 0) break;
            played_samples += frames;
        }
        if (r == MPG123_DONE) {
            if (req->use_ramp && !audio_should_stop() && mpg123_seek(mh, 0, SEEK_SET) >= 0) {
                played_samples = 0;
                continue;
            }
            break;
        }
        if (r == MPG123_NEW_FORMAT) continue;
        if (r != MPG123_OK) break;
    }

    if (pcm) {
        if (audio_should_stop()) snd_pcm_drop(pcm);
        else snd_pcm_drain(pcm);
    }

done:
    if (buf) free(buf);
    if (pcm) snd_pcm_close(pcm);
    if (mh) {
        mpg123_close(mh);
        mpg123_delete(mh);
    }

    int timed_out = 0;
    pthread_mutex_lock(&g_audio.lock);
    timed_out = g_audio.timed_out;
    pthread_mutex_unlock(&g_audio.lock);

    if (req->use_ramp) {
        alarm_volume_state_set(0, 0);
        if (timed_out) app_log("alarm", "Alarm stopped after the 30-minute safety limit");
    }

    pthread_mutex_lock(&g_state.lock);
    g_state.audio_playing = 0;
    g_state.audio_file[0] = '\0';
    clear_audio_metadata_locked();
    g_state.display_dirty = 1;
    pthread_mutex_unlock(&g_state.lock);

    /* Publish completion only after all old playback state is cleared.
       A waiting replacement track can then start without the exiting thread
       erasing the new track's metadata. */
    pthread_mutex_lock(&g_audio.lock);
    g_audio.running = 0;
    g_audio.stop_requested = 0;
    g_audio.alarm_mode = 0;
    g_audio.timed_out = 0;
    g_audio.alarm_deadline_ms = 0;
    g_audio.file[0] = '\0';
    pthread_cond_broadcast(&g_audio.stopped);
    pthread_mutex_unlock(&g_audio.lock);

    free(req);
    return NULL;
}

static void song_metadata_for_file(const char *path, const char *file,
                                   char *title, size_t title_len,
                                   char *artist, size_t artist_len) {
    struct mp_id3_metadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    (void)mp_read_id3_metadata(path, &metadata);
    if (!metadata.title[0]) mp_title_from_filename(file, metadata.title, sizeof(metadata.title));
    safe_str(title, title_len, metadata.title);
    safe_str(artist, artist_len, metadata.artist);
}

static void clear_audio_metadata_locked(void) {
    g_state.audio_title[0] = '\0';
    g_state.audio_artist[0] = '\0';
    g_state.audio_scroll_started_ms = 0;
}

static void audio_clear_visible_state(void) {
    alarm_volume_state_set(0, 0);

    pthread_mutex_lock(&g_state.lock);
    g_state.audio_playing = 0;
    g_state.audio_file[0] = '\0';
    clear_audio_metadata_locked();
    g_state.display_dirty = 1;
    pthread_mutex_unlock(&g_state.lock);
}

static void audio_request_stop(void) {
    pthread_mutex_lock(&g_audio.lock);
    if (g_audio.running) g_audio.stop_requested = 1;
    pthread_mutex_unlock(&g_audio.lock);
    audio_clear_visible_state();
}

static int audio_wait_stopped(unsigned int timeout_ms) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000u);
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&g_audio.lock);
    int wait_rc = 0;
    while (g_audio.running) {
        wait_rc = pthread_cond_timedwait(&g_audio.stopped, &g_audio.lock, &deadline);
        if (wait_rc == ETIMEDOUT || wait_rc != 0) break;
    }
    int stopped = !g_audio.running;
    pthread_mutex_unlock(&g_audio.lock);
    return stopped ? 0 : -1;
}

/*
 * Request-only stop for latency-sensitive callers such as IPC and touch.
 * The decoder thread publishes completion through g_audio.stopped.
 */
static void audio_stop(void) {
    audio_request_stop();
}

/*
 * Use only where the caller must know the old decoder has exited before it
 * can safely continue, such as track replacement and daemon shutdown.
 */
static int audio_stop_and_wait(unsigned int timeout_ms) {
    audio_request_stop();
    return audio_wait_stopped(timeout_ms);
}

static int audio_play_music_file(const char *music_file, int start_volume, int end_volume, int use_ramp) {
    char safe_file[MUSIC_FILE_MAX];
    char path[512];
    char song_title[MP_ID3_TEXT_MAX];
    char song_artist[MP_ID3_TEXT_MAX];
    int global_volume;
    int alarm_mode = use_ramp ? 1 : 0;

    safe_file[0] = '\0';
    path[0] = '\0';

    if (music_file && *music_file && safe_asset_filename(music_file) && has_mp3_ext(music_file)) {
        safe_str(safe_file, sizeof(safe_file), music_file);
    } else {
        (void)choose_random_music_file(safe_file, sizeof(safe_file));
    }

    if (safe_file[0]) {
        make_music_path(safe_file, path, sizeof(path));
        if (access(path, R_OK) != 0) path[0] = '\0';
    }

    /*
       The built-in alarm is deliberately stored outside the uploadable music
       directory. It therefore remains available when the music library is
       empty and cannot be removed through the music deletion endpoints.
    */
    if (!path[0]) {
        if (!alarm_mode || access(DEFAULT_ALARM_PATH, R_OK) != 0) {
            app_log(alarm_mode ? "alarm" : "music", "No playable audio file is available");
            return -1;
        }
        safe_str(safe_file, sizeof(safe_file), DEFAULT_ALARM_LABEL);
        safe_str(path, sizeof(path), DEFAULT_ALARM_PATH);
        app_log("alarm", "Using protected built-in alarm sound");
    }

    song_metadata_for_file(path, safe_file,
                           song_title, sizeof(song_title),
                           song_artist, sizeof(song_artist));

    start_volume = clamp_int(start_volume, 0, 100);
    end_volume = clamp_int(end_volume, 0, 100);

    pthread_mutex_lock(&g_state.lock);
    global_volume = clamp_int(g_state.global_volume, 0, 100);
    pthread_mutex_unlock(&g_state.lock);

    /*
       Alarm playback intentionally ignores global volume.
       Per-alarm start/end volumes are absolute 0..100 values so the OLED
       indicator and the actual PCM sample scaling always match.
       Normal/manual music playback still passes global_volume as start=end.
    */
    if (!use_ramp) {
        start_volume = global_volume;
        end_volume = global_volume;
    }

    if (audio_stop_and_wait(3000u) != 0) return -3;

    struct audio_play_request *req = calloc(1, sizeof(*req));
    if (!req) return -1;
    safe_str(req->path, sizeof(req->path), path);
    safe_str(req->file, sizeof(req->file), safe_file);
    req->start_volume = clamp_int(start_volume, 0, 100);
    req->end_volume = clamp_int(end_volume, 0, 100);
    req->use_ramp = use_ramp ? 1 : 0;

    pthread_mutex_lock(&g_audio.lock);
    g_audio.running = 1;
    g_audio.stop_requested = 0;
    g_audio.alarm_mode = alarm_mode;
    g_audio.timed_out = 0;
    g_audio.alarm_deadline_ms = alarm_mode
        ? monotonic_millis() + (uint64_t)ALARM_MAX_DURATION_SECONDS * 1000u : 0;
    safe_str(g_audio.file, sizeof(g_audio.file), safe_file);
    pthread_mutex_unlock(&g_audio.lock);

    pthread_mutex_lock(&g_state.lock);
    g_state.audio_playing = 1;
    safe_str(g_state.audio_file, sizeof(g_state.audio_file), safe_file);
    safe_str(g_state.audio_title, sizeof(g_state.audio_title), song_title);
    safe_str(g_state.audio_artist, sizeof(g_state.audio_artist), song_artist);
    g_state.audio_scroll_started_ms = monotonic_millis();
    g_state.display_dirty = 1;
    pthread_mutex_unlock(&g_state.lock);

    pthread_t tid;
    if (pthread_create(&tid, NULL, audio_thread_main, req) != 0) {
        pthread_mutex_lock(&g_audio.lock);
        g_audio.running = 0;
        g_audio.stop_requested = 0;
        g_audio.alarm_mode = 0;
        g_audio.timed_out = 0;
        g_audio.alarm_deadline_ms = 0;
        g_audio.file[0] = '\0';
        pthread_cond_broadcast(&g_audio.stopped);
        pthread_mutex_unlock(&g_audio.lock);
        pthread_mutex_lock(&g_state.lock);
        g_state.audio_playing = 0;
        g_state.audio_file[0] = '\0';
        clear_audio_metadata_locked();
        g_state.display_dirty = 1;
        pthread_mutex_unlock(&g_state.lock);
        free(req);
        return -1;
    }
    pthread_detach(tid);
    return 0;

}

/*
 * Start the protected Weather-warning chime without replacing music or an
 * alarm. This mirrors the kid-clock message notification behaviour: the
 * notification is skipped while any other audio session is active.
 */
static int audio_play_weather_warning_chime(void) {
    if (access(MESSAGE_CHIME_PATH, R_OK) != 0) return -1;

    int volume;
    pthread_mutex_lock(&g_state.lock);
    volume = clamp_int(g_state.global_volume, 0, MESSAGE_CHIME_VOLUME_MAX);
    pthread_mutex_unlock(&g_state.lock);

    struct audio_play_request *req = calloc(1, sizeof(*req));
    if (!req) return -1;
    safe_str(req->path, sizeof(req->path), MESSAGE_CHIME_PATH);
    safe_str(req->file, sizeof(req->file), MESSAGE_CHIME_LABEL);
    req->start_volume = volume;
    req->end_volume = volume;
    req->use_ramp = 0;

    pthread_mutex_lock(&g_audio.lock);
    if (g_audio.running) {
        pthread_mutex_unlock(&g_audio.lock);
        free(req);
        return -4;
    }
    g_audio.running = 1;
    g_audio.stop_requested = 0;
    g_audio.alarm_mode = 0;
    g_audio.timed_out = 0;
    g_audio.alarm_deadline_ms = 0;
    safe_str(g_audio.file, sizeof(g_audio.file), MESSAGE_CHIME_LABEL);
    pthread_mutex_unlock(&g_audio.lock);

    pthread_t tid;
    if (pthread_create(&tid, NULL, audio_thread_main, req) != 0) {
        pthread_mutex_lock(&g_audio.lock);
        g_audio.running = 0;
        g_audio.stop_requested = 0;
        g_audio.alarm_mode = 0;
        g_audio.timed_out = 0;
        g_audio.alarm_deadline_ms = 0;
        g_audio.file[0] = '\0';
        pthread_cond_broadcast(&g_audio.stopped);
        pthread_mutex_unlock(&g_audio.lock);
        free(req);
        return -1;
    }
    pthread_detach(tid);
    return 0;
}


struct oled_network_diagnostics {
    char interface_name[IFNAMSIZ];
    char ip_address[INET_ADDRSTRLEN];
    char ssid[IW_ESSID_MAX_SIZE + 1];
    char hostname[64];
    int signal_percent;
    int signal_dbm;
    int signal_available;
};

static void read_oled_network_diagnostics(struct oled_network_diagnostics *info) {
    if (!info) return;
    memset(info, 0, sizeof(*info));
    if (gethostname(info->hostname, sizeof(info->hostname) - 1) != 0)
        safe_str(info->hostname, sizeof(info->hostname), "Unavailable");

    FILE *wireless = fopen("/proc/net/wireless", "r");
    if (wireless) {
        char line[512];
        while (fgets(line, sizeof(line), wireless)) {
            char name[IFNAMSIZ] = "";
            double quality = 0.0;
            double level = 0.0;
            if (sscanf(line, " %15[^:]: %*d %lf %lf", name, &quality, &level) == 3) {
                safe_str(info->interface_name, sizeof(info->interface_name), name);
                int percent = (int)((quality / 70.0) * 100.0 + 0.5);
                info->signal_percent = clamp_int(percent, 0, 100);
                int dbm = (int)level;
                if (dbm > 100) dbm -= 256;
                info->signal_dbm = dbm;
                info->signal_available = 1;
                break;
            }
        }
        fclose(wireless);
    }

    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;

    if (info->interface_name[0]) {
        struct iwreq request;
        char essid[IW_ESSID_MAX_SIZE + 1];
        memset(&request, 0, sizeof(request));
        memset(essid, 0, sizeof(essid));
        safe_str(request.ifr_name, sizeof(request.ifr_name), info->interface_name);
        request.u.essid.pointer = essid;
        request.u.essid.length = IW_ESSID_MAX_SIZE;
        if (ioctl(fd, SIOCGIWESSID, &request) == 0) {
            size_t length = request.u.essid.length;
            if (length > IW_ESSID_MAX_SIZE) length = IW_ESSID_MAX_SIZE;
            essid[length] = '\0';
            safe_str(info->ssid, sizeof(info->ssid), essid);
        }
    }

    struct ifreq interfaces[32];
    struct ifconf list;
    memset(&interfaces, 0, sizeof(interfaces));
    memset(&list, 0, sizeof(list));
    list.ifc_len = sizeof(interfaces);
    list.ifc_req = interfaces;
    if (ioctl(fd, SIOCGIFCONF, &list) == 0) {
        int best_score = -1;
        size_t count = (size_t)list.ifc_len / sizeof(struct ifreq);
        for (size_t i = 0; i < count; i++) {
            struct ifreq *candidate = &interfaces[i];
            if (candidate->ifr_addr.sa_family != AF_INET) continue;
            struct ifreq flags_request;
            memset(&flags_request, 0, sizeof(flags_request));
            safe_str(flags_request.ifr_name, sizeof(flags_request.ifr_name), candidate->ifr_name);
            if (ioctl(fd, SIOCGIFFLAGS, &flags_request) != 0) continue;
            if (!(flags_request.ifr_flags & IFF_UP) || (flags_request.ifr_flags & IFF_LOOPBACK)) continue;

            int score = 10;
            if (info->interface_name[0] && strcmp(candidate->ifr_name, info->interface_name) == 0) score = 100;
            else if (strncmp(candidate->ifr_name, "wl", 2) == 0) score = 70;
            else if (strncmp(candidate->ifr_name, "en", 2) == 0 ||
                     strncmp(candidate->ifr_name, "eth", 3) == 0) score = 50;
            if (score <= best_score) continue;

            char text[INET_ADDRSTRLEN] = "";
            struct sockaddr_in *address = (struct sockaddr_in *)&candidate->ifr_addr;
            if (!inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) continue;
            best_score = score;
            safe_str(info->ip_address, sizeof(info->ip_address), text);
            if (!info->interface_name[0])
                safe_str(info->interface_name, sizeof(info->interface_name), candidate->ifr_name);
        }
    }
    close(fd);
}

static void fit_oled_text(char *text, size_t text_len) {
    if (!text || text_len == 0) return;
    while (text[0] && text5x7_width(text, 1) > OLED_W - 4) {
        size_t length = strlen(text);
        if (length <= 3) break;
        text[length - 1] = '\0';
        length--;
        if (length >= 3) {
            text[length - 1] = '.';
            text[length - 2] = '.';
            text[length - 3] = '.';
        }
    }
}

static void draw_diagnostic_line(int y, const char *label, const char *value, uint8_t colour) {
    char line[96];
    snprintf(line, sizeof(line), "%s: %s", label, value && *value ? value : "Unavailable");
    fit_oled_text(line, sizeof(line));
    draw_text5x7(2, y, 1, line, colour);
}

static void draw_diagnostic_screen(void) {
    struct oled_network_diagnostics info;
    read_oled_network_diagnostics(&info);
    oled_clear_fb(0);
    draw_text5x7(2, 0, 1, "NETWORK DIAGNOSTICS", 15);
    draw_diagnostic_line(11, "WIFI", info.ssid, 12);
    char signal[48];
    if (info.signal_available)
        snprintf(signal, sizeof(signal), "%d%% %d dBm", info.signal_percent, info.signal_dbm);
    else
        safe_str(signal, sizeof(signal), "Unavailable");
    draw_diagnostic_line(22, "SIGNAL", signal, 12);
    draw_diagnostic_line(33, "IP", info.ip_address, 12);
    draw_diagnostic_line(44, "HOST", info.hostname, 12);
    draw_text5x7(2, 55, 1, "TAP TO CLOSE", 8);
    oled_flush_full();
}

static int diagnostic_screen_active(void) {
    int active;
    pthread_mutex_lock(&g_state.lock);
    active = g_state.display_mode == 3;
    pthread_mutex_unlock(&g_state.lock);
    return active;
}

static int open_diagnostic_screen(void) {
    int opened = 0;
    pthread_mutex_lock(&g_state.lock);
    if (!g_state.alarm_active && !g_state.audio_playing) {
        g_state.diagnostic_return_mode = g_state.display_mode;
        if (g_state.diagnostic_return_mode < 0 || g_state.diagnostic_return_mode > 1)
            g_state.diagnostic_return_mode = 0;
        g_state.display_mode = 3;
        g_state.diagnostic_until = time(NULL) + DIAGNOSTIC_SCREEN_SECONDS;
        g_state.display_dirty = 1;
        opened = 1;
    }
    pthread_mutex_unlock(&g_state.lock);
    if (opened) app_log("touch", "Eight-second hold opened OLED diagnostics");
    return opened;
}

static void close_diagnostic_screen(void) {
    int closed = 0;
    pthread_mutex_lock(&g_state.lock);
    if (g_state.display_mode == 3) {
        int return_mode = g_state.diagnostic_return_mode;
        if (return_mode < 0 || return_mode > 1) return_mode = 0;
        g_state.display_mode = return_mode;
        g_state.diagnostic_return_mode = 0;
        g_state.diagnostic_until = 0;
        g_state.display_dirty = 1;
        closed = 1;
    }
    pthread_mutex_unlock(&g_state.lock);
    if (closed) app_log("touch", "OLED diagnostics closed");
}

/* ---------------- TTP223B touch input ---------------- */

static void touch_set_state(int ready, int pressed) {
    pthread_mutex_lock(&g_touch.lock);
    g_touch.ready = ready ? 1 : 0;
    g_touch.pressed = pressed ? 1 : 0;
    pthread_mutex_unlock(&g_touch.lock);
}

static void touch_get_state(int *ready, int *pressed) {
    pthread_mutex_lock(&g_touch.lock);
    if (ready) *ready = g_touch.ready;
    if (pressed) *pressed = g_touch.pressed;
    pthread_mutex_unlock(&g_touch.lock);
}

static int touch_read_active(void) {
    if (!g_touch.request) return -1;
    enum gpiod_line_value value =
        gpiod_line_request_get_value(g_touch.request, GPIO_TOUCH);
    if (value == GPIOD_LINE_VALUE_ACTIVE) return 1;
    if (value == GPIOD_LINE_VALUE_INACTIVE) return 0;
    return -1;
}

static int touch_init(void) {
    struct gpiod_line_settings *settings = NULL;
    struct gpiod_line_config *line_cfg = NULL;
    struct gpiod_request_config *req_cfg = NULL;
    unsigned int offset = GPIO_TOUCH;
    int rc = -1;

    g_touch.chip = gpiod_chip_open(GPIO_CHIP);
    if (!g_touch.chip) {
        perror("touch gpiod_chip_open");
        return -1;
    }

    settings = gpiod_line_settings_new();
    line_cfg = gpiod_line_config_new();
    req_cfg = gpiod_request_config_new();
    if (!settings || !line_cfg || !req_cfg) {
        fprintf(stderr, "failed to allocate touch GPIO request config\n");
        goto done;
    }

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings) != 0) {
        perror("touch gpiod_line_config_add_line_settings");
        goto done;
    }

    gpiod_request_config_set_consumer(req_cfg, APP_NAME "-touch");
    g_touch.request = gpiod_chip_request_lines(g_touch.chip, req_cfg, line_cfg);
    if (!g_touch.request) {
        perror("touch gpiod_chip_request_lines");
        goto done;
    }

    int initial = touch_read_active();
    if (initial < 0) {
        perror("touch gpiod_line_request_get_value");
        goto done;
    }

    touch_set_state(1, initial);
    rc = 0;

done:
    if (settings) gpiod_line_settings_free(settings);
    if (line_cfg) gpiod_line_config_free(line_cfg);
    if (req_cfg) gpiod_request_config_free(req_cfg);
    if (rc != 0) {
        if (g_touch.request) {
            gpiod_line_request_release(g_touch.request);
            g_touch.request = NULL;
        }
        if (g_touch.chip) {
            gpiod_chip_close(g_touch.chip);
            g_touch.chip = NULL;
        }
        touch_set_state(0, 0);
    }
    return rc;
}

static void touch_close(void) {
    touch_set_state(0, 0);
    if (g_touch.request) {
        gpiod_line_request_release(g_touch.request);
        g_touch.request = NULL;
    }
    if (g_touch.chip) {
        gpiod_chip_close(g_touch.chip);
        g_touch.chip = NULL;
    }
}

static int audio_is_playing(void) {
    int playing;
    pthread_mutex_lock(&g_state.lock);
    playing = g_state.audio_playing;
    pthread_mutex_unlock(&g_state.lock);
    return playing;
}

static void *touch_thread_main(void *arg) {
    (void)arg;
    int raw = touch_read_active();
    if (raw < 0) raw = 0;
    int last_raw = raw;
    int stable = raw;
    int action_consumed = 0;
    int diagnostic_opened_this_press = 0;
    int diagnostic_exit_press = 0;
    uint64_t now_ms = monotonic_millis();
    uint64_t raw_changed_ms = now_ms;
    uint64_t pressed_ms = stable ? now_ms : 0;
    touch_set_state(1, stable);

    while (g_running) {
        usleep(TOUCH_POLL_MS * 1000u);
        int current = touch_read_active();
        if (current < 0) continue;
        now_ms = monotonic_millis();

        if (current != last_raw) {
            last_raw = current;
            raw_changed_ms = now_ms;
        }

        if (current != stable && now_ms - raw_changed_ms >= TOUCH_DEBOUNCE_MS) {
            stable = current;
            touch_set_state(1, stable);
            if (stable) {
                pressed_ms = now_ms;
                action_consumed = 0;
                diagnostic_opened_this_press = 0;
                diagnostic_exit_press = diagnostic_screen_active();
            } else {
                uint64_t held_ms = pressed_ms > 0 && now_ms >= pressed_ms ? now_ms - pressed_ms : 0;
                if (diagnostic_exit_press) {
                    close_diagnostic_screen();
                    action_consumed = 1;
                } else if (diagnostic_opened_this_press || action_consumed) {
                    /* The diagnostic gesture has already consumed this press. */
                } else if (held_ms >= TOUCH_LONG_PRESS_MS) {
                    app_log("touch", "Long press requested random music");
                    audio_play_music_file("", 0, 0, 0);
                } else if (audio_is_playing()) {
                    app_log("touch", "Short press requested audio stop");
                    audio_request_stop();
                }
                pressed_ms = 0;
            }
        }

        if (stable && !action_consumed && !diagnostic_exit_press && pressed_ms > 0 &&
            now_ms - pressed_ms >= TOUCH_DIAGNOSTIC_PRESS_MS) {
            if (open_diagnostic_screen()) {
                action_consumed = 1;
                diagnostic_opened_this_press = 1;
            }
        }
    }

    touch_set_state(0, 0);
    return NULL;
}

/* ---------------- Private binary IPC ---------------- */

static const char *oled_color_name_for_id(int id) {
    switch (id) {
        case MP_OLED_COLOR_YELLOW: return "yellow";
        case MP_OLED_COLOR_WHITE: return "white";
        case MP_OLED_COLOR_GREEN:
        default: return "green";
    }
}

static const char *oled_font_name_for_id(int id) {
    switch (id) {
        case 1: return "Seven Thin";
        case 2: return "Pixel";
        case 3: return "Pixel Bold";
        case SYSTEM_DEFAULT_FONT_ID: return "Automatic font detection";
        default: return "Seven Segment";
    }
}

static void oled_font_choice_name(const char *font_file, int font_id, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (!font_file || !font_file[0]) {
        safe_str(out, out_len, oled_font_name_for_id(font_id));
        return;
    }

    char path[MP_SYSTEM_FONT_PATH_MAX];
    make_font_path(font_file, path, sizeof(path));
    pthread_mutex_lock(&g_font.lock);
    if (g_font.face && strcmp(g_font.loaded_file, font_file) == 0) {
        const char *family = g_font.face->family_name ? g_font.face->family_name : "";
        const char *style = g_font.face->style_name ? g_font.face->style_name : "";
        if (family[0] && style[0] && strcasecmp(style, "Regular") != 0 &&
            strcasecmp(style, "Book") != 0)
            snprintf(out, out_len, "%s %s", family, style);
        else if (family[0])
            safe_str(out, out_len, family);
    }
    pthread_mutex_unlock(&g_font.lock);

    if (!out[0] && path[0]) {
        const char *base = strrchr(path, '/');
        safe_str(out, out_len, base ? base + 1 : path);
        char *dot = strrchr(out, '.');
        if (dot && (strcasecmp(dot, ".ttf") == 0 || strcasecmp(dot, ".otf") == 0)) *dot = '\0';
    }
    if (!out[0]) safe_str(out, out_len, font_file);
}

static time_t next_alarm_time(const struct alarm_slot alarms[MAX_ALARMS], time_t now,
                              int *alarm_id) {
    struct tm base;
    localtime_r(&now, &base);
    time_t best = 0;
    int best_id = 0;

    for (int day_offset = 0; day_offset <= 7; day_offset++) {
        struct tm candidate_day = base;
        candidate_day.tm_mday += day_offset;
        candidate_day.tm_hour = 12;
        candidate_day.tm_min = 0;
        candidate_day.tm_sec = 0;
        candidate_day.tm_isdst = -1;
        time_t normalized = mktime(&candidate_day);
        if (normalized == (time_t)-1) continue;
        localtime_r(&normalized, &candidate_day);

        for (int i = 0; i < MAX_ALARMS; i++) {
            const struct alarm_slot *alarm = &alarms[i];
            if (!alarm->enabled || !(alarm->weekdays & (1 << candidate_day.tm_wday))) continue;
            struct tm candidate = candidate_day;
            candidate.tm_hour = alarm->hour;
            candidate.tm_min = alarm->min;
            candidate.tm_sec = 0;
            candidate.tm_isdst = -1;
            time_t when = mktime(&candidate);
            if (when <= now) continue;
            if (!best || when < best) {
                best = when;
                best_id = i + 1;
            }
        }
        if (best) break;
    }
    if (alarm_id) *alarm_id = best_id;
    return best;
}

static void format_next_alarm(time_t value, int clock_24h_mode, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    if (value <= 0) {
        safe_str(out, out_len, "No alarm scheduled");
        return;
    }
    time_t now = time(NULL);
    struct tm current;
    struct tm alarm;
    localtime_r(&now, &current);
    localtime_r(&value, &alarm);
    char time_text[32];
    strftime(time_text, sizeof(time_text), clock_24h_mode ? "%H:%M" : "%I:%M %p", &alarm);
    if (!clock_24h_mode && time_text[0] == '0') memmove(time_text, time_text + 1, strlen(time_text));

    struct tm today = current;
    today.tm_hour = 0;
    today.tm_min = 0;
    today.tm_sec = 0;
    today.tm_isdst = -1;
    time_t today_start = mktime(&today);
    long days = today_start == (time_t)-1 ? -1 : (long)((value - today_start) / 86400);
    if (days == 0) snprintf(out, out_len, "Today at %s", time_text);
    else if (days == 1) snprintf(out, out_len, "Tomorrow at %s", time_text);
    else {
        char weekday[24];
        strftime(weekday, sizeof(weekday), "%A", &alarm);
        snprintf(out, out_len, "%s at %s", weekday, time_text);
    }
}

static const char *display_mode_name(int mode) {
    switch (mode) {
        case 1: return "clear";
        case 3: return "diagnostics";
        default: return "clock";
    }
}

static int ipc_send_response(int client, unsigned int status, unsigned int content_type,
                             const void *body, size_t body_len) {
    if (body_len > MP_IPC_MAX_PAYLOAD) return -1;
    struct mp_ipc_response_header header = {
        .magic = MP_IPC_MAGIC,
        .version = MP_IPC_VERSION,
        .status = (uint16_t)status,
        .body_len = (uint32_t)body_len,
        .content_type = (uint16_t)content_type,
        .reserved = 0
    };
    return mp_send_packet(client, &header, sizeof(header), body, body_len);
}

static int ipc_send_json(int client, unsigned int status, const char *json) {
    const char *body = json ? json : "{}";
    return ipc_send_response(client, status, MP_IPC_CONTENT_JSON, body, strlen(body));
}

static int ipc_send_builder(int client, unsigned int status, struct mp_buffer *buffer) {
    size_t length = 0;
    char *body = mp_buffer_steal(buffer, &length);
    mp_buffer_free(buffer);
    if (!body) return ipc_send_json(client, 500, "{\"ok\":false,\"error\":\"JSON response exceeded its limit\"}");
    int rc = ipc_send_response(client, status, MP_IPC_CONTENT_JSON, body, length);
    free(body);
    return rc;
}

static int ipc_bad_payload(int client) {
    return ipc_send_json(client, 400, "{\"ok\":false,\"error\":\"invalid IPC payload\"}");
}

static int ipc_status(int client) {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    char timestr[64];
    char datestr[96];
    strftime(timestr, sizeof(timestr), "%I:%M %p", &tmv);
    if (timestr[0] == '0') memmove(timestr, timestr + 1, strlen(timestr));
    strftime(datestr, sizeof(datestr), "%A %B %e, %Y", &tmv);
    long uptime_seconds = (g_start_time > 0 && now >= g_start_time) ? (long)(now - g_start_time) : 0;

    pthread_mutex_lock(&g_state.lock);
    int audio = g_state.audio_playing;
    int alarm_active = g_state.alarm_active;
    int alarm_volume_percent = g_state.alarm_volume_percent;
    int mode = g_state.display_mode;
    int oled_ok = g_state.oled_ok;
    int font = g_state.oled_font;
    int font_size = g_state.oled_font_size;
    int global_volume = g_state.global_volume;
    int bedtime_enabled = g_state.bedtime_enabled;
    int bsh = g_state.bedtime_start_hour;
    int bsm = g_state.bedtime_start_min;
    int beh = g_state.bedtime_end_hour;
    int bem = g_state.bedtime_end_min;
    int bedtime_dim = g_state.bedtime_dim_percent;
    long long last_successful_alarm = g_state.last_successful_alarm;
    int weather_warning_chime_enabled = g_state.weather_warning_chime_enabled;
    int weather_warning_chime_during_bedtime = g_state.weather_warning_chime_during_bedtime;
    int clock_24h_mode = g_state.clock_24h_mode;
    int oled_color = g_state.oled_color;
    int oled_brightness = g_state.oled_brightness_current;
    int touch_ok = 0;
    int touch_pressed = 0;
    char font_file[128];
    char inside_font_file[128];
    char clock_name[64];
    char audio_file[MUSIC_FILE_MAX];
    char audio_title[MP_ID3_TEXT_MAX];
    char audio_artist[MP_ID3_TEXT_MAX];
    struct alarm_slot alarms[MAX_ALARMS];
    mp_safe_str(font_file, sizeof(font_file), g_state.oled_font_file);
    mp_safe_str(inside_font_file, sizeof(inside_font_file), g_state.inside_font_file);
    mp_safe_str(clock_name, sizeof(clock_name), g_state.clock_name);
    mp_safe_str(audio_file, sizeof(audio_file), g_state.audio_file);
    mp_safe_str(audio_title, sizeof(audio_title), g_state.audio_title);
    mp_safe_str(audio_artist, sizeof(audio_artist), g_state.audio_artist);
    memcpy(alarms, g_state.alarms, sizeof(alarms));
    pthread_mutex_unlock(&g_state.lock);
    touch_get_state(&touch_ok, &touch_pressed);

    int next_alarm_id = 0;
    time_t next_alarm_at = next_alarm_time(alarms, now, &next_alarm_id);
    char next_alarm_text[96];
    format_next_alarm(next_alarm_at, clock_24h_mode, next_alarm_text, sizeof(next_alarm_text));

    struct weather_dashboard_snapshot weather;
    memset(&weather, 0, sizeof(weather));
    weather_snapshot(&weather);

    struct room_sensor_snapshot room;
    memset(&room, 0, sizeof(room));
    room_sensor_snapshot(&room);

    char e_time[128], e_date[192], e_clock_name[160], e_audio_file[512];
    char e_audio_title[384], e_audio_artist[384], e_weather_location[192];
    char e_weather_warning[(MP_WEATHER_WARNING_TEXT_MAX * 6) + 1];
    char e_weather_warnings[MP_WEATHER_WARNING_SLOTS][(MP_WEATHER_WARNING_TEXT_MAX * 6) + 1];
    char e_font_file[256], e_font_name[256], e_inside_font_file[256];
    char e_room_device[256], e_room_error[384], e_next_alarm[192];
    char font_choice_name[256];
    oled_font_choice_name(font_file, font, font_choice_name, sizeof(font_choice_name));
    mp_json_escape(e_time, sizeof(e_time), timestr);
    mp_json_escape(e_date, sizeof(e_date), datestr);
    mp_json_escape(e_clock_name, sizeof(e_clock_name), clock_name);
    mp_json_escape(e_audio_file, sizeof(e_audio_file), audio_file);
    mp_json_escape(e_audio_title, sizeof(e_audio_title), audio_title);
    mp_json_escape(e_audio_artist, sizeof(e_audio_artist), audio_artist);
    mp_json_escape(e_weather_location, sizeof(e_weather_location), weather.location);
    const char *primary_weather_warning = weather.warning_count > 0
        ? weather.warning_descriptions[0] : "";
    mp_json_escape(e_weather_warning, sizeof(e_weather_warning), primary_weather_warning);
    for (int i = 0; i < MP_WEATHER_WARNING_SLOTS; i++)
        mp_json_escape(
            e_weather_warnings[i], sizeof(e_weather_warnings[i]),
            i < weather.warning_count ? weather.warning_descriptions[i] : ""
        );
    mp_json_escape(e_font_file, sizeof(e_font_file), font_file);
    mp_json_escape(e_font_name, sizeof(e_font_name), font_choice_name);
    mp_json_escape(e_inside_font_file, sizeof(e_inside_font_file), inside_font_file);
    mp_json_escape(e_room_device, sizeof(e_room_device), room.device);
    mp_json_escape(e_room_error, sizeof(e_room_error), room.error);
    mp_json_escape(e_next_alarm, sizeof(e_next_alarm), next_alarm_text);

    struct mp_buffer body;
    if (mp_buffer_init(&body, 4608, MP_IPC_MAX_PAYLOAD) != 0)
        return ipc_send_json(client, 500, "{\"ok\":false,\"error\":\"allocation failed\"}");
    mp_buffer_appendf(&body,
        "{\"time\":\"%s\",\"date\":\"%s\",\"clock_name\":\"%s\",\"app_version\":\"%s\","
        "\"uptime_seconds\":%ld,\"audio_file\":\"%s\",\"audio_title\":\"%s\",\"audio_artist\":\"%s\","
        "\"global_volume\":%d,"
        "\"bedtime_enabled\":%d,"
        "\"bedtime_start_hour\":%d,\"bedtime_start_min\":%d,\"bedtime_end_hour\":%d,\"bedtime_end_min\":%d,"
        "\"bedtime_dim_percent\":%d,\"weather_warning_chime_enabled\":%d,\"weather_warning_chime_during_bedtime\":%d,"
        "\"clock_24h_mode\":%d,\"oled_color\":\"%s\",\"bedtime_active\":%d,\"oled_brightness_percent\":%d,"
        "\"next_alarm_id\":%d,\"next_alarm_at\":%lld,\"next_alarm_text\":\"%s\",\"last_successful_alarm\":%lld,"
        "\"audio_playing\":%d,\"alarm_active\":%d,\"alarm_volume_percent\":%d,\"display_mode\":\"%s\",\"oled_ok\":%d,"
        "\"touch_ok\":%d,\"touch_pressed\":%d,\"touch_gpio\":%d,"
        "\"oled_font\":%d,\"oled_font_size\":%d,\"oled_font_file\":\"%s\",\"oled_font_name\":\"%s\","
        "\"inside_font_file\":\"%s\",\"weather\":{",
        e_time, e_date, e_clock_name, APP_VERSION, uptime_seconds, e_audio_file,
        e_audio_title, e_audio_artist, global_volume,
        bedtime_enabled,
        bsh, bsm, beh, bem, bedtime_dim, weather_warning_chime_enabled,
        weather_warning_chime_during_bedtime, clock_24h_mode, oled_color_name_for_id(oled_color),
        is_bedtime_now(), oled_brightness, next_alarm_id, (long long)next_alarm_at, e_next_alarm,
        last_successful_alarm, audio, alarm_active, alarm_volume_percent,
        display_mode_name(mode), oled_ok, touch_ok, touch_pressed, GPIO_TOUCH,
        font, font_size, e_font_file, e_font_name, e_inside_font_file);

    mp_buffer_appendf(&body,
        "\"location\":\"%s\",\"warning_type\":\"%s\",\"warning_count\":%d,\"warnings\":[",
        e_weather_location, e_weather_warning, weather.warning_count);
    for (int i = 0; i < weather.warning_count && !body.failed; i++)
        mp_buffer_appendf(&body, "%s\"%s\"", i ? "," : "", e_weather_warnings[i]);
    mp_buffer_appendf(&body,
        "],\"current_temperature_c\":%d,\"current_temperature_available\":%s,\"current_temperature_is_forecast\":%s,\"humidity_percent\":%d,\"humidity_available\":%s,\"precipitation_probability_percent\":%d,\"uv_index\":%d,"
        "\"observed_at\":%lld,\"slots\":[",
        weather.current_temperature_c,
        weather.current_temperature_available ? "true" : "false",
        weather.current_temperature_is_forecast ? "true" : "false",
        weather.humidity_percent, weather.humidity_available ? "true" : "false",
        weather.precipitation_probability_percent, weather.uv_index,
        (long long)weather.observed_at);

    for (int i = 0; i < MP_WEATHER_FORECAST_SLOTS && !body.failed; i++) {
        const struct weather_slot_state *slot = &weather.slots[i];
        mp_buffer_appendf(&body,
            "%s{\"kind\":\"%s\",\"label\":\"%s\",\"date_label\":\"%s\","
            "\"temperature_c\":%d,\"temperature_available\":%s,"
            "\"low_temperature_c\":%d,\"low_temperature_available\":%s,\"low_hour\":%d,"
            "\"high_temperature_c\":%d,\"high_temperature_available\":%s,\"high_hour\":%d,"
            "\"precipitation_probability_percent\":%d,\"icon\":%d}",
            i ? "," : "", mp_weather_slot_kind_name(slot->kind), slot->label,
            slot->date_label, slot->temperature_c,
            slot->temperature_available ? "true" : "false",
            slot->low_temperature_c,
            slot->low_temperature_available ? "true" : "false", slot->low_hour,
            slot->high_temperature_c,
            slot->high_temperature_available ? "true" : "false", slot->high_hour,
            slot->precipitation_probability_percent, slot->icon);
    }

    mp_buffer_appendf(&body,
        "]},\"room_sensor\":{\"type\":\"AHT10\",\"enabled\":%s,\"status\":\"%s\","
        "\"device\":\"%s\",\"address\":\"0x%02X\",\"poll_seconds\":%d,\"stale_seconds\":%d,"
        "\"temperature_c\":%.1f,\"humidity_percent\":%.1f,\"measured_at\":%lld,\"error\":\"%s\"},"
        "\"alarms\":[",
        room.enabled ? "true" : "false", room_sensor_status_name(room.status),
        e_room_device, room.address, room.poll_seconds, room.stale_seconds,
        room.temperature_c, room.humidity_percent, (long long)room.measured_at,
        e_room_error);

    for (int i = 0; i < MAX_ALARMS && !body.failed; i++) {
        mp_buffer_appendf(&body,
            "%s{\"id\":%d,\"enabled\":%d,\"hour\":%d,\"min\":%d,\"weekdays\":%d,\"start_volume\":%d,\"end_volume\":%d,\"music_file\":\"",
            i ? "," : "", i + 1, alarms[i].enabled, alarms[i].hour, alarms[i].min,
            alarms[i].weekdays, alarms[i].start_volume, alarms[i].end_volume);
        mp_buffer_append_json_string(&body, alarms[i].music_file);
        mp_buffer_append(&body, "\"}");
    }
    mp_buffer_append(&body, "]}");
    return ipc_send_builder(client, 200, &body);
}

static int ipc_logs_get(int client) {
    pthread_mutex_lock(&g_log_lock);
    FILE *f = fopen(LOG_FILE, "rb");
    if (!f) {
        pthread_mutex_unlock(&g_log_lock);
        return ipc_send_json(client, 200, "{\"ok\":true,\"log_file\":\"" LOG_FILE "\",\"entries\":[]}");
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        pthread_mutex_unlock(&g_log_lock);
        return ipc_send_json(client, 500, "{\"ok\":false,\"entries\":[]}");
    }
    long size = ftell(f);
    if (size < 0) size = 0;
    long start = size > LOG_MAX_BYTES ? size - LOG_MAX_BYTES : 0;
    if (fseek(f, start, SEEK_SET) != 0) start = 0;
    size_t cap = (size_t)(size - start);
    char *buf = malloc(cap + 1);
    if (!buf) {
        fclose(f);
        pthread_mutex_unlock(&g_log_lock);
        return ipc_send_json(client, 500, "{\"ok\":false,\"entries\":[]}");
    }
    size_t got = fread(buf, 1, cap, f);
    fclose(f);
    buf[got] = '\0';
    pthread_mutex_unlock(&g_log_lock);

    char *first = buf;
    if (start > 0) {
        char *nl = strchr(buf, '\n');
        if (nl && nl[1]) first = nl + 1;
    }
    char *lines[LOG_VIEW_LINES];
    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(first, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (*line) lines[count++ % LOG_VIEW_LINES] = line;
    }
    struct mp_buffer body;
    if (mp_buffer_init(&body, 4096, MP_IPC_MAX_PAYLOAD) != 0) {
        free(buf);
        return ipc_send_json(client, 500, "{\"ok\":false,\"entries\":[]}");
    }
    mp_buffer_append(&body, "{\"ok\":true,\"log_file\":\"");
    mp_buffer_append_json_string(&body, LOG_FILE);
    mp_buffer_append(&body, "\",\"entries\":[");
    int n = count < LOG_VIEW_LINES ? count : LOG_VIEW_LINES;
    int start_idx = count > LOG_VIEW_LINES ? count % LOG_VIEW_LINES : 0;
    for (int i = 0; i < n && !body.failed; i++) {
        int idx = (start_idx + i) % LOG_VIEW_LINES;
        mp_buffer_appendf(&body, "%s\"", i ? "," : "");
        mp_buffer_append_json_string(&body, lines[idx]);
        mp_buffer_append(&body, "\"");
    }
    mp_buffer_append(&body, "]}");
    free(buf);
    return ipc_send_builder(client, 200, &body);
}

static int ipc_logs_clear(int client) {
    pthread_mutex_lock(&g_log_lock);
    ensure_dir(CONFIG_DIR);
    FILE *f = fopen(LOG_FILE, "w");
    if (f) fclose(f);
    pthread_mutex_unlock(&g_log_lock);
    app_log("log", "Log cleared through API");
    return ipc_send_json(client, 200, "{\"ok\":true}");
}

static int ipc_display_action(int client, const struct mp_ipc_display_action *request) {
    switch (request->action) {
        case MP_IPC_ACTION_CLOCK:
            pthread_mutex_lock(&g_state.lock);
            g_state.display_mode = 0;
            g_state.display_dirty = 1;
            pthread_mutex_unlock(&g_state.lock);
            app_log("action", "Show clock requested");
            break;
        case MP_IPC_ACTION_CLEAR:
            pthread_mutex_lock(&g_state.lock);
            g_state.display_mode = 1;
            g_state.display_dirty = 1;
            pthread_mutex_unlock(&g_state.lock);
            app_log("action", "Clear screen requested");
            break;
        case MP_IPC_ACTION_STOP_AUDIO:
            audio_request_stop();
            app_log("action", "Stop audio requested");
            break;
        case MP_IPC_ACTION_PLAY_MUSIC: {
            int volume;
            pthread_mutex_lock(&g_state.lock);
            volume = g_state.global_volume;
            pthread_mutex_unlock(&g_state.lock);
            audio_play_music_file(request->file, volume, volume, 0);
            app_log("action", "Play music requested: %s", request->file);
            break;
        }
        default:
            return ipc_send_json(client, 400, "{\"ok\":false,\"error\":\"unknown display action\"}");
    }
    return ipc_send_json(client, 200, "{\"ok\":true}");
}

static int ipc_config_alarm(int client, const struct mp_ipc_alarm_config *request) {
    int id = clamp_int(request->id, 1, MAX_ALARMS);
    struct alarm_slot alarm = {
        .enabled = request->enabled ? 1 : 0,
        .hour = clamp_int(request->hour, 0, 23),
        .min = clamp_int(request->minute, 0, 59),
        .weekdays = request->weekdays ? request->weekdays : 0x7f,
        .start_volume = clamp_int(request->start_volume, 0, 100),
        .end_volume = clamp_int(request->end_volume, 0, 100),
        .fired_yday = -1,
        .music_file = ""
    };
    if (safe_asset_filename(request->music_file) && has_mp3_ext(request->music_file)) {
        char path[512];
        make_music_path(request->music_file, path, sizeof(path));
        if (access(path, R_OK) == 0) mp_safe_str(alarm.music_file, sizeof(alarm.music_file), request->music_file);
    }
    pthread_mutex_lock(&g_state.lock);
    g_state.alarms[id - 1] = alarm;
    pthread_mutex_unlock(&g_state.lock);
    save_config();
    app_log("alarm", "Saved alarm %d", id);
    return ipc_send_json(client, 200, "{\"ok\":true}");
}

static int ipc_config_audio(int client, const struct mp_ipc_audio_config *request) {
    int changed = 0;
    int volume = -1;
    pthread_mutex_lock(&g_state.lock);
    if (request->present_mask & MP_IPC_AUDIO_GLOBAL_VOLUME) {
        volume = clamp_int(request->global_volume, 0, 100);
        g_state.global_volume = volume;
        changed = 1;
    }
    pthread_mutex_unlock(&g_state.lock);
    if (!changed)
        return ipc_send_json(client, 400, "{\"ok\":false,\"error\":\"no audio settings supplied\"}");
    save_config();
    if (volume >= 0) app_log("music", "Saved global volume %d%%", volume);
    return ipc_send_json(client, 200, "{\"ok\":true}");
}

static int ipc_config_personalization(int client, const struct mp_ipc_personalization_config *request) {
    char name[64];
    mp_safe_str(name, sizeof(name), request->clock_name);
    sanitize_clock_name(name);
    pthread_mutex_lock(&g_state.lock);
    mp_safe_str(g_state.clock_name, sizeof(g_state.clock_name), name);
    pthread_mutex_unlock(&g_state.lock);
    save_config();
    app_log("settings", "Saved clock name %s", name);
    return ipc_send_json(client, 200, "{\"ok\":true}");
}

static int ipc_display_preview(int client) {
    pthread_mutex_lock(&g_state.lock);
    int oled_ok = g_state.oled_ok;
    pthread_mutex_unlock(&g_state.lock);
    if (!oled_ok)
        return ipc_send_json(client, 503, "{\"ok\":false,\"error\":\"OLED unavailable\"}");

    uint8_t snapshot[OLED_FB_BYTES];
    pthread_mutex_lock(&g_oled.preview_lock);
    memcpy(snapshot, g_oled.preview_fb, sizeof(snapshot));
    pthread_mutex_unlock(&g_oled.preview_lock);
    return ipc_send_response(client, 200, MP_IPC_CONTENT_BINARY, snapshot, sizeof(snapshot));
}

static int ipc_brightness_preview(int client, const struct mp_ipc_brightness_preview *request) {
    int percent = clamp_int(request->percent, 0, 100);
    int hold_seconds = clamp_int(request->hold_seconds, 1, 30);
    pthread_mutex_lock(&g_state.lock);
    if (!g_state.oled_ok) {
        pthread_mutex_unlock(&g_state.lock);
        return ipc_send_json(client, 503, "{\"ok\":false,\"error\":\"OLED unavailable\"}");
    }
    g_state.brightness_preview_percent = percent;
    g_state.brightness_preview_until = time(NULL) + hold_seconds;
    g_state.display_dirty = 1;
    pthread_mutex_unlock(&g_state.lock);

    /* The main OLED loop applies the preview. Keeping SPI writes on that thread
       prevents a slider request from interleaving commands with a framebuffer flush. */
    char body[96];
    snprintf(body, sizeof(body), "{\"ok\":true,\"brightness_percent\":%d}", percent);
    return ipc_send_json(client, 200, body);
}

static int ipc_config_display(int client, const struct mp_ipc_display_config *request) {
    pthread_mutex_lock(&g_state.lock);
    int font = g_state.oled_font;
    int font_size = g_state.oled_font_size;
    int bedtime_enabled = g_state.bedtime_enabled;
    int bedtime_dim = g_state.bedtime_dim_percent;
    int warning_chime_enabled = g_state.weather_warning_chime_enabled;
    int warning_chime_during_bedtime = g_state.weather_warning_chime_during_bedtime;
    int clock_mode = g_state.clock_24h_mode;
    int oled_color = g_state.oled_color;
    int bsh = g_state.bedtime_start_hour;
    int bsm = g_state.bedtime_start_min;
    int beh = g_state.bedtime_end_hour;
    int bem = g_state.bedtime_end_min;
    char font_file[128];
    char inside_font_file[128];
    mp_safe_str(font_file, sizeof(font_file), g_state.oled_font_file);
    mp_safe_str(inside_font_file, sizeof(inside_font_file), g_state.inside_font_file);
    pthread_mutex_unlock(&g_state.lock);

    if (request->present_mask & MP_IPC_DISPLAY_FONT) font = clamp_int(request->oled_font, 0, SYSTEM_DEFAULT_FONT_ID);
    if (request->present_mask & MP_IPC_DISPLAY_FONT_SIZE) font_size = clamp_int(request->oled_font_size, 18, 54);
    if (request->present_mask & MP_IPC_DISPLAY_BEDTIME_ENABLED) bedtime_enabled = request->bedtime_enabled ? 1 : 0;
    if (request->present_mask & MP_IPC_DISPLAY_BEDTIME_DIM) bedtime_dim = clamp_int(request->bedtime_dim_percent, 0, 100);
    if (request->present_mask & MP_IPC_DISPLAY_WARNING_CHIME_ENABLED)
        warning_chime_enabled = request->weather_warning_chime_enabled ? 1 : 0;
    if (request->present_mask & MP_IPC_DISPLAY_WARNING_CHIME_BEDTIME)
        warning_chime_during_bedtime = request->weather_warning_chime_during_bedtime ? 1 : 0;
    if (request->present_mask & MP_IPC_DISPLAY_CLOCK_MODE) clock_mode = request->clock_24h_mode ? 1 : 0;
    if (request->present_mask & MP_IPC_DISPLAY_OLED_COLOR)
        oled_color = clamp_int(request->oled_color, MP_OLED_COLOR_YELLOW, MP_OLED_COLOR_WHITE);
    if (request->present_mask & MP_IPC_DISPLAY_BEDTIME_START) {
        bsh = clamp_int(request->bedtime_start_hour, 0, 23);
        bsm = clamp_int(request->bedtime_start_minute, 0, 59);
    }
    if (request->present_mask & MP_IPC_DISPLAY_BEDTIME_END) {
        beh = clamp_int(request->bedtime_end_hour, 0, 23);
        bem = clamp_int(request->bedtime_end_minute, 0, 59);
    }
    if (request->present_mask & MP_IPC_DISPLAY_FONT_FILE) {
        font_file[0] = '\0';
        if (request->oled_font_file[0]) {
            int valid_choice = mp_system_font_is_key(request->oled_font_file) ||
                (safe_asset_filename(request->oled_font_file) && has_font_ext(request->oled_font_file));
            if (valid_choice) {
                char path[MP_SYSTEM_FONT_PATH_MAX];
                make_font_path(request->oled_font_file, path, sizeof(path));
                if (path[0] && access(path, R_OK) == 0)
                    mp_safe_str(font_file, sizeof(font_file), request->oled_font_file);
            }
        }
    }
    if (request->present_mask & MP_IPC_DISPLAY_INSIDE_FONT_FILE) {
        inside_font_file[0] = '\0';
        if (request->inside_font_file[0]) {
            int valid_choice = mp_system_font_is_key(request->inside_font_file) ||
                (safe_asset_filename(request->inside_font_file) && has_font_ext(request->inside_font_file));
            if (valid_choice) {
                char path[MP_SYSTEM_FONT_PATH_MAX];
                make_font_path(request->inside_font_file, path, sizeof(path));
                if (path[0] && access(path, R_OK) == 0)
                    mp_safe_str(inside_font_file, sizeof(inside_font_file), request->inside_font_file);
            }
        }
    }

    pthread_mutex_lock(&g_state.lock);
    int changed = g_state.oled_font_size != font_size ||
        strcmp(g_state.oled_font_file, font_file) != 0 ||
        strcmp(g_state.inside_font_file, inside_font_file) != 0;
    g_state.oled_font = font;
    g_state.oled_font_size = font_size;
    g_state.clock_24h_mode = clock_mode;
    g_state.oled_color = oled_color;
    g_state.bedtime_enabled = bedtime_enabled;
    g_state.bedtime_dim_percent = bedtime_dim;
    g_state.weather_warning_chime_enabled = warning_chime_enabled;
    g_state.weather_warning_chime_during_bedtime = warning_chime_during_bedtime;
    g_state.bedtime_start_hour = bsh;
    g_state.bedtime_start_min = bsm;
    g_state.bedtime_end_hour = beh;
    g_state.bedtime_end_min = bem;
    mp_safe_str(g_state.oled_font_file, sizeof(g_state.oled_font_file), font_file);
    mp_safe_str(g_state.inside_font_file, sizeof(g_state.inside_font_file), inside_font_file);
    g_state.display_dirty = 1;
    pthread_mutex_unlock(&g_state.lock);
    if (!font_file[0] && font == SYSTEM_DEFAULT_FONT_ID)
        changed |= apply_default_font_selection();
    if (changed) font_cache_reset();
    save_config();
    app_log("display", "Saved display settings");
    return ipc_send_json(client, 200, "{\"ok\":true}");
}


static void sanitize_weather_label(char *label, size_t label_len, const char *fallback) {
    if (!label || label_len == 0) return;
    char out[8];
    size_t j = 0;
    for (const unsigned char *p = (const unsigned char *)label; *p && j + 1 < sizeof(out); p++) {
        if (isalnum(*p)) out[j++] = (char)toupper(*p);
    }
    out[j] = '\0';
    if (!out[0]) safe_str(out, sizeof(out), fallback ? fallback : "INSIDE");
    safe_str(label, label_len, out);
}

static void sanitize_weather_date_label(char *label, size_t label_len) {
    if (!label || label_len == 0) return;
    char out[16];
    size_t j = 0;
    int previous_space = 1;
    for (const unsigned char *p = (const unsigned char *)label; *p && j + 1 < sizeof(out); p++) {
        if (isalnum(*p)) {
            out[j++] = (char)toupper(*p);
            previous_space = 0;
        } else if (isspace(*p) && !previous_space) {
            out[j++] = ' ';
            previous_space = 1;
        }
    }
    while (j > 0 && out[j - 1] == ' ') j--;
    out[j] = '\0';
    safe_str(label, label_len, out);
}

static int ipc_weather_update(int client, const struct mp_ipc_weather_update *request) {
    if (!request) return ipc_bad_payload(client);

    struct weather_slot_state slots[MP_WEATHER_FORECAST_SLOTS];
    for (int i = 0; i < MP_WEATHER_FORECAST_SLOTS; i++) {
        memset(&slots[i], 0, sizeof(slots[i]));
        int kind = request->slots[i].kind;
        if (kind < MP_WEATHER_SLOT_ROOM || kind > MP_WEATHER_SLOT_TODAY)
            kind = i == 0 ? MP_WEATHER_SLOT_ROOM : MP_WEATHER_SLOT_FORECAST;
        slots[i].kind = kind;
        slots[i].temperature_c = clamp_int(request->slots[i].temperature_c, -99, 99);
        slots[i].temperature_available = request->slots[i].temperature_available != 0;
        slots[i].low_temperature_c =
            clamp_int(request->slots[i].low_temperature_c, -99, 99);
        slots[i].low_temperature_available =
            request->slots[i].low_temperature_available != 0;
        slots[i].low_hour = clamp_int(request->slots[i].low_hour, 0, 23);
        slots[i].high_temperature_c =
            clamp_int(request->slots[i].high_temperature_c, -99, 99);
        slots[i].high_temperature_available =
            request->slots[i].high_temperature_available != 0;
        slots[i].high_hour = clamp_int(request->slots[i].high_hour, 0, 23);
        slots[i].precipitation_probability_percent =
            clamp_int(request->slots[i].precipitation_probability_percent, 0, 100);
        slots[i].icon = clamp_int(request->slots[i].icon, MP_WEATHER_ICON_UNKNOWN, MP_WEATHER_ICON_FOG);
        safe_str(slots[i].label, sizeof(slots[i].label), request->slots[i].label);
        sanitize_weather_label(slots[i].label, sizeof(slots[i].label),
                               mp_weather_slot_default_label(kind));
        safe_str(slots[i].date_label, sizeof(slots[i].date_label), request->slots[i].date_label);
        sanitize_weather_date_label(slots[i].date_label, sizeof(slots[i].date_label));
    }

    char incoming_warnings[MP_WEATHER_WARNING_SLOTS][MP_WEATHER_WARNING_TEXT_MAX];
    memset(incoming_warnings, 0, sizeof(incoming_warnings));
    int incoming_warning_count = clamp_int(request->warning_count, 0, MP_WEATHER_WARNING_SLOTS);
    int compacted_warning_count = 0;
    for (int i = 0; i < incoming_warning_count; i++) {
        if (!request->warning_descriptions[i][0]) continue;
        safe_str(
            incoming_warnings[compacted_warning_count],
            sizeof(incoming_warnings[compacted_warning_count]),
            request->warning_descriptions[i]
        );
        compacted_warning_count++;
    }
    incoming_warning_count = compacted_warning_count;

    int new_warning_added = 0;
    pthread_mutex_lock(&g_weather.lock);
    safe_str(g_weather.location, sizeof(g_weather.location), request->location);

    for (int i = 0; i < incoming_warning_count && !new_warning_added; i++) {
        int already_present = 0;
        for (int j = 0; j < g_weather.warning_count; j++) {
            if (strcmp(g_weather.warning_descriptions[j], incoming_warnings[i]) == 0) {
                already_present = 1;
                break;
            }
        }
        if (!already_present) new_warning_added = 1;
    }

    int warning_set_changed = g_weather.warning_count != incoming_warning_count;
    if (!warning_set_changed) {
        for (int i = 0; i < incoming_warning_count; i++) {
            if (strcmp(g_weather.warning_descriptions[i], incoming_warnings[i]) != 0) {
                warning_set_changed = 1;
                break;
            }
        }
    }
    if (warning_set_changed) {
        g_weather.warning_count = incoming_warning_count;
        memset(g_weather.warning_descriptions, 0, sizeof(g_weather.warning_descriptions));
        memcpy(g_weather.warning_descriptions, incoming_warnings, sizeof(incoming_warnings));
    }
    g_weather.current_temperature_c = clamp_int(request->current_temperature_c, -99, 99);
    g_weather.current_temperature_available = request->current_temperature_available != 0;
    g_weather.current_temperature_is_forecast = request->current_temperature_is_forecast != 0;
    g_weather.humidity_percent = clamp_int(request->humidity_percent, 0, 100);
    g_weather.humidity_available = request->humidity_available != 0;
    g_weather.precipitation_probability_percent =
        clamp_int(request->precipitation_probability_percent, 0, 100);
    g_weather.uv_index = clamp_int(request->uv_index, 0, 99);
    g_weather.observed_at = request->observed_at > 0 ? (time_t)request->observed_at : time(NULL);
    memcpy(g_weather.slots, slots, sizeof(slots));
    pthread_mutex_unlock(&g_weather.lock);

    int warning_chime_enabled;
    int warning_chime_during_bedtime;
    pthread_mutex_lock(&g_state.lock);
    warning_chime_enabled = g_state.weather_warning_chime_enabled;
    warning_chime_during_bedtime = g_state.weather_warning_chime_during_bedtime;
    g_state.display_dirty = 1;
    pthread_mutex_unlock(&g_state.lock);

    if (new_warning_added && warning_chime_enabled) {
        if (!warning_chime_during_bedtime && is_bedtime_now()) {
            app_log("weather", "Weather warning chime suppressed during bedtime: %s",
                    incoming_warnings[0]);
        } else {
            int chime_result = audio_play_weather_warning_chime();
            if (chime_result == -4)
                app_log("weather", "Weather warning chime skipped because audio is already playing");
            else if (chime_result != 0)
                app_log("weather", "Weather warning chime could not be played");
            else
                app_log("weather", "Weather warning chime played: %s", incoming_warnings[0]);
        }
    }

    app_log("weather", "Updated weather dashboard: %s%dC%s, %s humidity",
            request->current_temperature_available ? "" : "unavailable ",
            request->current_temperature_c,
            request->current_temperature_is_forecast ? " hourly forecast" : "",
            request->humidity_available ? "reported" : "unavailable");
    return ipc_send_json(client, 200, "{\"ok\":true}");
}

static int ipc_asset_event(int client, const struct mp_ipc_asset_event *event) {
    if (event->action == MP_IPC_ASSET_UPLOADED) {
        if (event->kind == MP_IPC_ASSET_FONT) {
            pthread_mutex_lock(&g_state.lock);
            g_state.oled_font = SYSTEM_DEFAULT_FONT_ID;
            mp_safe_str(g_state.oled_font_file, sizeof(g_state.oled_font_file), event->file);
            g_state.display_dirty = 1;
            pthread_mutex_unlock(&g_state.lock);
            font_cache_reset();
            save_config();
        } else if (event->kind != MP_IPC_ASSET_MUSIC) {
            return ipc_send_json(client, 400, "{\\\"ok\\\":false,\\\"error\\\":\\\"invalid asset kind\\\"}");
        }
        app_log("assets", "Uploaded %u asset(s), first %s", event->count, event->file);
    } else if (event->action == MP_IPC_ASSET_DELETED) {
        if (event->kind == MP_IPC_ASSET_MUSIC) {
            int stop_audio = 0;
            int config_changed = 0;
            pthread_mutex_lock(&g_state.lock);
            stop_audio = strcmp(g_state.audio_file, event->file) == 0;
            pthread_mutex_unlock(&g_state.lock);
            if (stop_audio) audio_stop();
            pthread_mutex_lock(&g_state.lock);
            if (stop_audio) {
                g_state.audio_playing = 0;
                g_state.audio_file[0] = '\0';
            }
            for (int i = 0; i < MAX_ALARMS; i++) {
                if (strcmp(g_state.alarms[i].music_file, event->file) == 0) {
                    g_state.alarms[i].music_file[0] = '\0';
                    config_changed = 1;
                }
            }
            g_state.display_dirty = 1;
            pthread_mutex_unlock(&g_state.lock);
            if (config_changed) save_config();
        } else if (event->kind == MP_IPC_ASSET_FONT) {
            pthread_mutex_lock(&g_state.lock);
            if (strcmp(g_state.oled_font_file, event->file) == 0) {
                g_state.oled_font_file[0] = '\0';
                g_state.oled_font = SYSTEM_DEFAULT_FONT_ID;
            }
            if (strcmp(g_state.inside_font_file, event->file) == 0)
                g_state.inside_font_file[0] = '\0';
            g_state.display_dirty = 1;
            pthread_mutex_unlock(&g_state.lock);
            (void)apply_default_font_selection();
            font_cache_reset();
            save_config();
        } else {
            return ipc_send_json(client, 400, "{\\\"ok\\\":false,\\\"error\\\":\\\"invalid asset kind\\\"}");
        }
        app_log("assets", "Deleted asset %s", event->file);
    } else if (event->action == MP_IPC_ASSET_DELETED_ALL) {
        if (event->kind != MP_IPC_ASSET_MUSIC)
            return ipc_send_json(client, 400, "{\\\"ok\\\":false,\\\"error\\\":\\\"invalid asset kind\\\"}");
        audio_stop();
        pthread_mutex_lock(&g_state.lock);
        g_state.audio_playing = 0;
        g_state.audio_file[0] = '\0';
        for (int i = 0; i < MAX_ALARMS; i++) g_state.alarms[i].music_file[0] = '\0';
        g_state.display_dirty = 1;
        pthread_mutex_unlock(&g_state.lock);
        save_config();
        app_log("assets", "Deleted all music assets (%u files)", event->count);
    } else {
        return ipc_send_json(client, 400, "{\\\"ok\\\":false,\\\"error\\\":\\\"invalid asset event\\\"}");
    }
    return ipc_send_json(client, 200, "{\\\"ok\\\":true}");
}

static int ipc_asset_state(int client) {
    struct mp_ipc_asset_state state;
    memset(&state, 0, sizeof(state));
    pthread_mutex_lock(&g_state.lock);
    state.global_volume = g_state.global_volume;
    state.builtin_font = g_state.oled_font;
    state.font_size = g_state.oled_font_size;
    mp_safe_str(state.current_music, sizeof(state.current_music), g_state.audio_file);
    mp_safe_str(state.selected_font, sizeof(state.selected_font), g_state.oled_font_file);
    pthread_mutex_unlock(&g_state.lock);
    return ipc_send_response(client, 200, MP_IPC_CONTENT_BINARY, &state, sizeof(state));
}

static int config_read_full(int fd, void *buffer, size_t length) {
    unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t got = read(fd, cursor, length);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return -1;
        cursor += (size_t)got;
        length -= (size_t)got;
    }
    return 0;
}

static int config_write_full(int fd, const void *buffer, size_t length) {
    const unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t wrote = write(fd, cursor, length);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) return -1;
        cursor += (size_t)wrote;
        length -= (size_t)wrote;
    }
    return 0;
}

static int ipc_config_export(int client) {
    save_config();
    int fd = open(CONFIG_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return ipc_send_json(client, 500, "{\"ok\":false,\"error\":\"configuration could not be read\"}");
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uint64_t)st.st_size > MP_IPC_CONFIG_MAX_BYTES) {
        close(fd);
        return ipc_send_json(client, 500, "{\"ok\":false,\"error\":\"configuration is invalid or too large\"}");
    }
    char *data = malloc((size_t)st.st_size);
    if (!data) {
        close(fd);
        return ipc_send_json(client, 500, "{\"ok\":false,\"error\":\"allocation failed\"}");
    }
    int ok = config_read_full(fd, data, (size_t)st.st_size) == 0;
    close(fd);
    if (!ok) {
        free(data);
        return ipc_send_json(client, 500, "{\"ok\":false,\"error\":\"configuration could not be read\"}");
    }
    int rc = ipc_send_response(client, 200, MP_IPC_CONTENT_TEXT, data, (size_t)st.st_size);
    free(data);
    return rc;
}

static int config_blob_valid(const struct mp_ipc_config_blob *blob) {
    if (!blob || blob->length == 0 || blob->length > MP_IPC_CONFIG_MAX_BYTES) return 0;
    int has_clock_name = 0;
    int has_alarm = 0;
    for (uint32_t i = 0; i < blob->length; i++) {
        unsigned char ch = (unsigned char)blob->data[i];
        if (ch == 0 || ch == '\r') return 0;
        if (ch < 0x09 || (ch > 0x0a && ch < 0x20)) return 0;
    }
    char copy[MP_IPC_CONFIG_MAX_BYTES + 1u];
    memcpy(copy, blob->data, blob->length);
    copy[blob->length] = '\0';
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strchr(line, '=')) return 0;
        if (strncmp(line, "clock_name=", 11) == 0) has_clock_name = 1;
        if (strncmp(line, "alarm1_enabled=", 15) == 0) has_alarm = 1;
    }
    return has_clock_name && has_alarm;
}

static int ipc_config_import(int client, const struct mp_ipc_config_blob *blob) {
    if (!config_blob_valid(blob))
        return ipc_send_json(client, 400, "{\"ok\":false,\"error\":\"backup configuration is invalid\"}");
    (void)audio_stop_and_wait(3000u);
    ensure_dir(CONFIG_DIR);
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), CONFIG_FILE ".restore");
    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0640);
    int failed = fd < 0;
    if (!failed && (config_write_full(fd, blob->data, blob->length) != 0 || fsync(fd) != 0)) failed = 1;
    if (fd >= 0 && close(fd) != 0) failed = 1;
    if (failed) {
        unlink(tmp_path);
        return ipc_send_json(client, 500, "{\"ok\":false,\"error\":\"configuration could not be restored\"}");
    }
    if (rename(tmp_path, CONFIG_FILE) != 0) {
        unlink(tmp_path);
        return ipc_send_json(client, 500, "{\"ok\":false,\"error\":\"configuration could not be activated\"}");
    }
    pthread_mutex_lock(&g_state.lock);
    reset_persistent_state_locked();
    pthread_mutex_unlock(&g_state.lock);
    load_config();
    (void)apply_default_font_selection();
    font_cache_reset();
    pthread_mutex_lock(&g_state.lock);
    g_state.display_mode = 0;
    g_state.display_dirty = 1;
    pthread_mutex_unlock(&g_state.lock);
    save_config();
    app_log("backup", "Configuration restored from backup");
    return ipc_send_json(client, 200, "{\"ok\":true}");
}

static int ipc_dispatch(int client, uint16_t opcode, const void *payload, size_t payload_len) {
#define EXPECT(type) do { if (payload_len != sizeof(type)) return ipc_bad_payload(client); } while (0)
    switch (opcode) {
        case MP_IPC_OP_STATUS:
            if (payload_len != 0) return ipc_bad_payload(client);
            return ipc_status(client);
        case MP_IPC_OP_DISPLAY_ACTION:
            EXPECT(struct mp_ipc_display_action);
            return ipc_display_action(client, payload);
        case MP_IPC_OP_CONFIG_ALARM:
            EXPECT(struct mp_ipc_alarm_config);
            return ipc_config_alarm(client, payload);
        case MP_IPC_OP_CONFIG_AUDIO:
            EXPECT(struct mp_ipc_audio_config);
            return ipc_config_audio(client, payload);
        case MP_IPC_OP_CONFIG_PERSONALIZATION:
            EXPECT(struct mp_ipc_personalization_config);
            return ipc_config_personalization(client, payload);
        case MP_IPC_OP_CONFIG_DISPLAY:
            EXPECT(struct mp_ipc_display_config);
            return ipc_config_display(client, payload);
        case MP_IPC_OP_WEATHER_UPDATE:
            EXPECT(struct mp_ipc_weather_update);
            return ipc_weather_update(client, payload);
        case MP_IPC_OP_LOGS_GET:
            if (payload_len != 0) return ipc_bad_payload(client);
            return ipc_logs_get(client);
        case MP_IPC_OP_LOGS_CLEAR:
            if (payload_len != 0) return ipc_bad_payload(client);
            return ipc_logs_clear(client);
        case MP_IPC_OP_ASSET_EVENT:
            EXPECT(struct mp_ipc_asset_event);
            return ipc_asset_event(client, payload);
        case MP_IPC_OP_ASSET_STATE:
            if (payload_len != 0) return ipc_bad_payload(client);
            return ipc_asset_state(client);
        case MP_IPC_OP_PING:
            if (payload_len != 0) return ipc_bad_payload(client);
            return ipc_send_json(client, 200, "{\"ok\":true}");
        case MP_IPC_OP_DISPLAY_PREVIEW:
            if (payload_len != 0) return ipc_bad_payload(client);
            return ipc_display_preview(client);
        case MP_IPC_OP_BRIGHTNESS_PREVIEW:
            EXPECT(struct mp_ipc_brightness_preview);
            return ipc_brightness_preview(client, payload);
        case MP_IPC_OP_CONFIG_EXPORT:
            if (payload_len != 0) return ipc_bad_payload(client);
            return ipc_config_export(client);
        case MP_IPC_OP_CONFIG_IMPORT:
            EXPECT(struct mp_ipc_config_blob);
            return ipc_config_import(client, payload);
        default:
            return ipc_send_json(client, 404, "{\"ok\":false,\"error\":\"unknown IPC opcode\"}");
    }
#undef EXPECT
}

static void set_ipc_timeouts(int client) {
    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int socket_buffer = (int)(MP_IPC_MAX_PAYLOAD * 2u);
    (void)setsockopt(client, SOL_SOCKET, SO_RCVBUF, &socket_buffer, sizeof(socket_buffer));
    (void)setsockopt(client, SOL_SOCKET, SO_SNDBUF, &socket_buffer, sizeof(socket_buffer));
}

static uid_t expected_api_uid(void) {
    const char *value = getenv("MK_PICLOCK_API_USER");
    if (!value || !*value) value = "mk-piclock-api";
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);
    if (!errno && end != value && *end == '\0') return (uid_t)parsed;
    struct passwd *entry = getpwnam(value);
    return entry ? entry->pw_uid : (uid_t)-1;
}

static int ipc_peer_allowed(int client, uid_t api_uid) {
#ifdef SO_PEERCRED
    struct ucred credentials;
    socklen_t length = sizeof(credentials);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) return 0;
    return credentials.uid == 0 || (api_uid != (uid_t)-1 && credentials.uid == api_uid);
#else
    (void)client;
    (void)api_uid;
    return 0;
#endif
}

static ssize_t ipc_expected_payload_size(uint16_t opcode) {
    switch (opcode) {
        case MP_IPC_OP_STATUS:
        case MP_IPC_OP_LOGS_GET:
        case MP_IPC_OP_LOGS_CLEAR:
        case MP_IPC_OP_ASSET_STATE:
        case MP_IPC_OP_PING:
        case MP_IPC_OP_DISPLAY_PREVIEW:
        case MP_IPC_OP_CONFIG_EXPORT:
            return 0;
        case MP_IPC_OP_DISPLAY_ACTION: return sizeof(struct mp_ipc_display_action);
        case MP_IPC_OP_BRIGHTNESS_PREVIEW: return sizeof(struct mp_ipc_brightness_preview);
        case MP_IPC_OP_CONFIG_ALARM: return sizeof(struct mp_ipc_alarm_config);
        case MP_IPC_OP_CONFIG_AUDIO: return sizeof(struct mp_ipc_audio_config);
        case MP_IPC_OP_CONFIG_PERSONALIZATION: return sizeof(struct mp_ipc_personalization_config);
        case MP_IPC_OP_CONFIG_DISPLAY: return sizeof(struct mp_ipc_display_config);
        case MP_IPC_OP_WEATHER_UPDATE: return sizeof(struct mp_ipc_weather_update);
        case MP_IPC_OP_ASSET_EVENT: return sizeof(struct mp_ipc_asset_event);
        case MP_IPC_OP_CONFIG_IMPORT: return sizeof(struct mp_ipc_config_blob);
        default: return -1;
    }
}

static void handle_ipc_client(int client) {
    void *packet = NULL;
    size_t packet_len = 0;
    if (mp_recv_packet_alloc(client, &packet,
                             sizeof(struct mp_ipc_request_header) + MP_IPC_MAX_PAYLOAD,
                             &packet_len) != 0 || packet_len < sizeof(struct mp_ipc_request_header)) {
        free(packet);
        return;
    }
    struct mp_ipc_request_header header;
    memcpy(&header, packet, sizeof(header));
    if (header.magic != MP_IPC_MAGIC) {
        (void)ipc_send_json(client, 400, "{\"ok\":false,\"error\":\"invalid IPC magic\"}");
        free(packet);
        return;
    }
    if (header.version != MP_IPC_VERSION) {
        (void)ipc_send_json(client, 409, "{\"ok\":false,\"error\":\"IPC protocol version mismatch\"}");
        free(packet);
        return;
    }
    ssize_t expected = ipc_expected_payload_size(header.opcode);
    if (expected < 0) {
        (void)ipc_send_json(client, 404, "{\"ok\":false,\"error\":\"unknown IPC opcode\"}");
        free(packet);
        return;
    }
    if (header.payload_len != (uint32_t)expected ||
        packet_len != sizeof(header) + header.payload_len) {
        (void)ipc_send_json(client, 400, "{\"ok\":false,\"error\":\"invalid IPC payload length\"}");
        free(packet);
        return;
    }
    const void *payload = header.payload_len
        ? (const unsigned char *)packet + sizeof(header) : NULL;
    (void)ipc_dispatch(client, header.opcode, payload, header.payload_len);
    free(packet);
}

static void *ipc_thread_main(void *arg) {
    (void)arg;
    if (mkdir(CORE_RUNTIME_DIR, 0770) != 0 && errno != EEXIST) {
        perror("mkdir core runtime");
        return NULL;
    }
    uid_t api_uid = expected_api_uid();
    if (api_uid == (uid_t)-1)
        fprintf(stderr, "warning: mk-piclock-api user not found; only root IPC peers will be accepted\n");
    int server = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (server < 0) {
        perror("core socket");
        return NULL;
    }
    int socket_buffer = (int)(MP_IPC_MAX_PAYLOAD * 2u);
    (void)setsockopt(server, SOL_SOCKET, SO_RCVBUF, &socket_buffer, sizeof(socket_buffer));
    (void)setsockopt(server, SOL_SOCKET, SO_SNDBUF, &socket_buffer, sizeof(socket_buffer));
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(CORE_SOCKET_PATH) >= sizeof(addr.sun_path)) {
        close(server);
        return NULL;
    }
    mp_safe_str(addr.sun_path, sizeof(addr.sun_path), CORE_SOCKET_PATH);
    unlink(CORE_SOCKET_PATH);
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        chmod(CORE_SOCKET_PATH, 0660) != 0 || listen(server, 8) != 0) {
        perror("bind/listen core IPC");
        close(server);
        unlink(CORE_SOCKET_PATH);
        return NULL;
    }
    fprintf(stderr, "private SOCK_SEQPACKET IPC listening on %s\n", CORE_SOCKET_PATH);
    while (g_running) {
        struct pollfd pfd = {.fd = server, .events = POLLIN};
        int ready = poll(&pfd, 1, 500);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0 || !(pfd.revents & POLLIN)) continue;
        int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        set_ipc_timeouts(client);
        if (!ipc_peer_allowed(client, api_uid)) {
            (void)ipc_send_json(client, 403, "{\"ok\":false,\"error\":\"IPC peer rejected\"}");
            close(client);
            continue;
        }
        handle_ipc_client(client);
        close(client);
    }
    close(server);
    unlink(CORE_SOCKET_PATH);
    return NULL;
}


/* ---------------- Main loop ---------------- */

static void check_alarm(void) {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    struct alarm_slot fire_alarm;
    int fire = 0;

    pthread_mutex_lock(&g_state.lock);
    for (int i = 0; i < MAX_ALARMS; i++) {
        struct alarm_slot *a = &g_state.alarms[i];
        int weekday_ok = (a->weekdays & (1 << tmv.tm_wday)) != 0;
        if (a->enabled && weekday_ok && tmv.tm_hour == a->hour && tmv.tm_min == a->min && a->fired_yday != tmv.tm_yday) {
            a->fired_yday = tmv.tm_yday;
            fire_alarm = *a;
            fire = 1;
            break;
        }
    }
    if (fire) g_state.display_mode = 0;
    pthread_mutex_unlock(&g_state.lock);

    if (fire) {
        app_log("alarm", "Alarm fired at %02d:%02d", fire_alarm.hour, fire_alarm.min);
        if (audio_play_music_file(fire_alarm.music_file, fire_alarm.start_volume, fire_alarm.end_volume, 1) != 0)
            app_log("alarm", "Alarm could not start because no playable audio was available");
    }
}

int main(void) {
    g_start_time = time(NULL);

    if (mpg123_init() != MPG123_OK) {
        fprintf(stderr, "mpg123_init failed\n");
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    srand((unsigned int)(time(NULL) ^ getpid()));

    ensure_dir(APP_ROOT);
    ensure_dir(MUSIC_DIR);
    ensure_dir(FONT_DIR);
    ensure_dir(CONFIG_DIR);
    init_alarm_defaults();
    load_config();
    if (apply_default_font_selection())
        app_log("display", "Applied automatic clock font selection");
    pthread_mutex_lock(&g_state.lock);
    g_state.font_policy_version = FONT_POLICY_VERSION;
    pthread_mutex_unlock(&g_state.lock);
    save_config(); /* normalize the file and discard retired configuration keys */
    app_log("system", "mk-clock-adult %s starting", APP_VERSION);
    room_sensor_configure();

    int touch_available = (touch_init() == 0);
    if (touch_available) app_log("system", "TTP223B touch input initialized on GPIO %d", GPIO_TOUCH);
    else app_log("system", "TTP223B touch input unavailable on GPIO %d", GPIO_TOUCH);

    if (oled_init() == 0) {
        pthread_mutex_lock(&g_state.lock);
        g_state.oled_ok = 1;
        pthread_mutex_unlock(&g_state.lock);
        app_log("system", "OLED initialized");
        apply_bedtime_brightness();
        draw_weather_dashboard_screen();
    } else {
        app_log("system", "OLED init failed, core IPC will still start");
        fprintf(stderr, "OLED init failed, core IPC will still start\n");
    }

    pthread_t ipc_thread;
    pthread_t touch_thread;
    pthread_t room_sensor_thread;
    int touch_thread_started = 0;
    int room_sensor_thread_started = 0;
    app_log("system", "Private core IPC listening on %s", CORE_SOCKET_PATH);

    struct room_sensor_snapshot room_config;
    memset(&room_config, 0, sizeof(room_config));
    room_sensor_snapshot(&room_config);
    if (room_config.enabled) {
        if (pthread_create(&room_sensor_thread, NULL, room_sensor_thread_main, NULL) == 0) {
            room_sensor_thread_started = 1;
        } else {
            pthread_mutex_lock(&g_room_sensor.lock);
            g_room_sensor.status = ROOM_SENSOR_ERROR;
            safe_str(g_room_sensor.error, sizeof(g_room_sensor.error),
                     "unable to start AHT10 polling thread");
            pthread_mutex_unlock(&g_room_sensor.lock);
            app_log("room-sensor", "Unable to start AHT10 polling thread");
        }
    } else {
        app_log("room-sensor", "AHT10 room sensor disabled");
    }

    if (touch_available) {
        if (pthread_create(&touch_thread, NULL, touch_thread_main, NULL) == 0) {
            touch_thread_started = 1;
        } else {
            app_log("system", "Unable to start touch input thread");
            touch_close();
        }
    }

    if (pthread_create(&ipc_thread, NULL, ipc_thread_main, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    int last_min = -1;
    int last_mode = -1;
    int last_colon_phase = -1;
    uint64_t last_diagnostic_refresh_ms = 0;
    while (g_running) {
        check_alarm();
        apply_bedtime_brightness();

        pthread_mutex_lock(&g_state.lock);
        int mode = g_state.display_mode;
        int dirty = g_state.display_dirty;
        g_state.display_dirty = 0;
        int oled_ok = g_state.oled_ok;
        pthread_mutex_unlock(&g_state.lock);

        int footer_refresh_active = 0;
        if (oled_ok) {
            if (mode == 3) {
                time_t diagnostic_until;
                pthread_mutex_lock(&g_state.lock);
                diagnostic_until = g_state.diagnostic_until;
                pthread_mutex_unlock(&g_state.lock);
                if (diagnostic_until <= 0 || time(NULL) >= diagnostic_until) {
                    close_diagnostic_screen();
                    pthread_mutex_lock(&g_state.lock);
                    mode = g_state.display_mode;
                    pthread_mutex_unlock(&g_state.lock);
                    last_mode = -1;
                } else {
                    uint64_t diagnostic_now_ms = monotonic_millis();
                    if (dirty || mode != last_mode ||
                        diagnostic_now_ms - last_diagnostic_refresh_ms >= DIAGNOSTIC_REFRESH_MS) {
                        draw_diagnostic_screen();
                        last_diagnostic_refresh_ms = diagnostic_now_ms;
                    }
                }
            }

            if (mode == 0) {
                time_t now = time(NULL);
                struct tm tmv;
                localtime_r(&now, &tmv);
                int colon_phase = clock_colon_blink_phase();
                footer_refresh_active = dashboard_footer_refresh_active();
                if (dirty || mode != last_mode || tmv.tm_min != last_min || colon_phase != last_colon_phase) {
                    last_min = tmv.tm_min;
                    last_colon_phase = colon_phase;
                    draw_weather_dashboard_screen();
                } else if (footer_refresh_active) {
                    refresh_dashboard_footer();
                }
            } else if (mode == 1) {
                if (dirty || mode != last_mode) {
                    oled_clear_fb(0);
                    oled_flush_full();
                }
            }
        }

        last_mode = mode;
        usleep(footer_refresh_active ? DASHBOARD_MARQUEE_FRAME_US : 250000u);
    }

    if (touch_thread_started) pthread_join(touch_thread, NULL);
    if (room_sensor_thread_started) pthread_join(room_sensor_thread, NULL);
    touch_close();
    (void)audio_stop_and_wait(3000u);
    pthread_mutex_lock(&g_font.lock);
    font_cache_close_locked();
    pthread_mutex_unlock(&g_font.lock);
    oled_close();
    mpg123_exit();
    return 0;
}
