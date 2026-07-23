/*
 * mk-piclock-api.c
 *
 * Public libmicrohttpd gateway for mk-piclock.
 * HTTP terminates here. The hardware daemon uses a compact binary IPC protocol.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mntent.h>
#include <linux/wireless.h>
#include <microhttpd.h>
#include <netinet/in.h>
#include <signal.h>
#include <pwd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/timex.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "asset_store.h"
#include "font_catalog.h"
#include "ipc_protocol.h"
#include "io_helpers.h"
#include "music_jobs.h"
#include "util.h"


#include "weather_source_store.h"
#include "weather_frames.h"
#include "weather_version.h"
#define API_NAME "mk-clock-adult-api"
#define API_VERSION "1.44"
#define PRODUCT_VERSION "mk-clock-adult-1.2.62"
#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#define DEFAULT_PUBLIC_BIND "0.0.0.0"
#define DEFAULT_PUBLIC_PORT 8080
#define CORE_SOCKET_PATH "/run/mk-piclock/core.sock"
#define WEB_DIR "/opt/mk-piclock/web"
#define API_DOC_DIR "/opt/mk-piclock/api"
#define MAX_REQUEST_BODY (128U * 1024U * 1024U)
#define MAX_STATIC_FILE (64U * 1024U * 1024U)
#define MAX_API_JSON_RESPONSE (1024U * 1024U)
#define ALLOWED_ORIGIN_MAX 256
#define DISK_RESERVE_BYTES (64ULL * 1024ULL * 1024ULL)
#define DEFAULT_MUSIC_QUOTA_BYTES (1024ULL * 1024ULL * 1024ULL)
#define SOCKET_TIMEOUT_SEC 15
#define MHD_THREAD_POOL_SIZE 2
#define MHD_CONNECTION_LIMIT 12
#define FORM_FIELDS_MAX 64
#define FORM_KEY_MAX 64
#define FORM_VALUE_MAX 512
#define UPLOAD_FILES_MAX 256
#define MAX_BACKUP_BYTES (64ULL * 1024ULL * 1024ULL)
#define BACKUP_ENTRY_MAX 512

static volatile sig_atomic_t g_running = 1;
static char g_allowed_origin[ALLOWED_ORIGIN_MAX];
static uid_t g_expected_core_uid = (uid_t)-1;
static uint64_t g_music_quota_bytes = DEFAULT_MUSIC_QUOTA_BYTES;
static uint64_t g_disk_reserve_bytes = DISK_RESERVE_BYTES;


enum route_id {
    ROUTE_STATUS,
    ROUTE_HEALTH,
    ROUTE_DIAGNOSTICS,
    ROUTE_DIAGNOSTIC_REPORT,
    ROUTE_BACKUP_DOWNLOAD,
    ROUTE_BACKUP_RESTORE,
    ROUTE_FONTS_LIST,
    ROUTE_MUSIC_LIST,
    ROUTE_MUSIC_JOBS,
    ROUTE_CLEAR_MUSIC_QUEUE,
    ROUTE_FONT_FILE,
    ROUTE_UPLOAD_MUSIC,
    ROUTE_UPLOAD_FONT,
    ROUTE_DELETE_MUSIC,
    ROUTE_DELETE_ALL_MUSIC,
    ROUTE_DELETE_FONT,
    ROUTE_DISPLAY_ACTION,
    ROUTE_DISPLAY_PREVIEW,
    ROUTE_BRIGHTNESS_PREVIEW,
    ROUTE_CONFIG_ALARM,
    ROUTE_CONFIG_AUDIO,
    ROUTE_CONFIG_PERSONALIZATION,
    ROUTE_CONFIG_DISPLAY,
    ROUTE_WEATHER_ACTIVITY_GET,
    ROUTE_WEATHER_FRAMES_GET,
    ROUTE_WEATHER_FRAMES_SET,
    ROUTE_WEATHER_SOURCE_GET,
    ROUTE_WEATHER_SOURCE_SET,
    ROUTE_WEATHER_UPDATE,
    ROUTE_LOGS,
    ROUTE_LOGS_CLEAR
};

struct api_route {
    const char *method;
    const char *path;
    enum route_id id;
};

static const struct api_route g_routes[] = {
    {"GET",  "/api/v1/status",                         ROUTE_STATUS},
    {"GET",  "/api/v1/health",                         ROUTE_HEALTH},
    {"GET",  "/api/v1/diagnostics",                    ROUTE_DIAGNOSTICS},
    {"GET",  "/api/v1/diagnostics/report",             ROUTE_DIAGNOSTIC_REPORT},
    {"GET",  "/api/v1/backup/download",                 ROUTE_BACKUP_DOWNLOAD},
    {"POST", "/api/v1/backup/restore",                  ROUTE_BACKUP_RESTORE},
    {"GET",  "/api/v1/assets/fonts",                    ROUTE_FONTS_LIST},
    {"GET",  "/api/v1/assets/music",                    ROUTE_MUSIC_LIST},
    {"GET",  "/api/v1/assets/music/jobs",               ROUTE_MUSIC_JOBS},
    {"POST", "/api/v1/assets/music/jobs/clear",         ROUTE_CLEAR_MUSIC_QUEUE},
    {"GET",  "/api/v1/assets/fonts/file",               ROUTE_FONT_FILE},
    {"POST", "/api/v1/assets/music/upload",             ROUTE_UPLOAD_MUSIC},
    {"POST", "/api/v1/assets/fonts/upload",             ROUTE_UPLOAD_FONT},
    {"POST", "/api/v1/assets/music/delete",            ROUTE_DELETE_MUSIC},
    {"POST", "/api/v1/assets/music/delete-all",        ROUTE_DELETE_ALL_MUSIC},
    {"POST", "/api/v1/assets/fonts/delete",            ROUTE_DELETE_FONT},
    {"POST", "/api/v1/display/action",                  ROUTE_DISPLAY_ACTION},
    {"GET",  "/api/v1/display/preview",                 ROUTE_DISPLAY_PREVIEW},
    {"POST", "/api/v1/display/brightness-preview",      ROUTE_BRIGHTNESS_PREVIEW},
    {"POST", "/api/v1/config/alarms",                   ROUTE_CONFIG_ALARM},
    {"POST", "/api/v1/config/audio",                    ROUTE_CONFIG_AUDIO},
    {"POST", "/api/v1/config/personalization",          ROUTE_CONFIG_PERSONALIZATION},
    {"POST", "/api/v1/config/display",                  ROUTE_CONFIG_DISPLAY},
    {"GET",  "/api/v1/weather/activity",              ROUTE_WEATHER_ACTIVITY_GET},
    {"GET",  "/api/v1/config/weather-frames",          ROUTE_WEATHER_FRAMES_GET},
    {"POST", "/api/v1/config/weather-frames",          ROUTE_WEATHER_FRAMES_SET},
    {"GET",  "/api/v1/config/weather-source",          ROUTE_WEATHER_SOURCE_GET},
    {"POST", "/api/v1/config/weather-source",          ROUTE_WEATHER_SOURCE_SET},
    {"POST", "/api/v1/weather",                         ROUTE_WEATHER_UPDATE},
    {"GET",  "/api/v1/logs",                            ROUTE_LOGS},
    {"POST", "/api/v1/logs/clear",                      ROUTE_LOGS_CLEAR}
};

struct form_field {
    char key[FORM_KEY_MAX];
    char value[FORM_VALUE_MAX];
    size_t len;
};

struct upload_file {
    char key[FORM_KEY_MAX];
    char filename[MP_ASSET_NAME_MAX];
    char temp_path[512];
    int fd;
    uint64_t next_offset;
    size_t size;
};

struct request_context {
    const struct api_route *route;
    struct MHD_PostProcessor *post;
    struct form_field *fields;
    size_t field_count;
    size_t field_cap;
    struct upload_file *uploads;
    size_t upload_count;
    size_t upload_cap;
    size_t received_bytes;
    int parse_failed;
    int body_too_large;
    int response_queued;
    size_t upload_limit;
};

struct ipc_result {
    unsigned int status;
    unsigned int content_type;
    unsigned char *body;
    size_t body_len;
};

static int ipc_call(uint16_t opcode, const void *payload, size_t payload_len, struct ipc_result *result);
static void ipc_result_free(struct ipc_result *result);

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static int public_port_from_env(void) {
    const char *value = getenv("MK_PICLOCK_API_PORT");
    if (!value || !*value) return DEFAULT_PUBLIC_PORT;
    char *end = NULL;
    errno = 0;
    long port = strtol(value, &end, 10);
    if (errno || end == value || *end || port < 1 || port > 65535) return -1;
    return (int)port;
}

static const char *public_bind_from_env(void) {
    const char *value = getenv("MK_PICLOCK_API_BIND");
    return value && *value ? value : DEFAULT_PUBLIC_BIND;
}

static uint64_t bytes_from_env(const char *name, uint64_t fallback) {
    const char *value = getenv(name);
    if (!value || !*value) return fallback;
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (errno || end == value || *end) return fallback;
    return (uint64_t)parsed;
}

static uid_t resolve_user_uid(const char *environment_name, const char *default_user) {
    const char *value = getenv(environment_name);
    if (value && *value) {
        char *end = NULL;
        errno = 0;
        unsigned long parsed = strtoul(value, &end, 10);
        if (!errno && end != value && *end == '\0') return (uid_t)parsed;
        struct passwd *entry = getpwnam(value);
        if (entry) return entry->pw_uid;
    }
    struct passwd *entry = getpwnam(default_user);
    return entry ? entry->pw_uid : (uid_t)-1;
}



struct network_diagnostics {
    char hostname[256];
    char interface_name[IFNAMSIZ];
    char ip_address[INET_ADDRSTRLEN];
    char ssid[IW_ESSID_MAX_SIZE + 1];
    int wifi_signal_percent;
    int wifi_signal_dbm;
    int wifi_signal_available;
    int ntp_synchronized;
    int system_time_valid;
};

static int read_default_interface(char *out, size_t out_len) {
    if (!out || out_len == 0) return -1;
    out[0] = '\0';
    FILE *routes = fopen("/proc/net/route", "r");
    if (!routes) return -1;
    char line[512];
    if (!fgets(line, sizeof(line), routes)) {
        fclose(routes);
        return -1;
    }
    while (fgets(line, sizeof(line), routes)) {
        char name[IFNAMSIZ] = "";
        unsigned long destination = 1;
        unsigned long gateway = 0;
        unsigned int flags = 0;
        if (sscanf(line, "%15s %lx %lx %X", name, &destination, &gateway, &flags) >= 4 &&
            destination == 0 && (flags & 0x1U)) {
            mp_safe_str(out, out_len, name);
            break;
        }
    }
    fclose(routes);
    return out[0] ? 0 : -1;
}

static void find_primary_ipv4(struct network_diagnostics *info) {
    char preferred[IFNAMSIZ] = "";
    (void)read_default_interface(preferred, sizeof(preferred));

    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct ifreq interfaces[32];
    struct ifconf list;
    memset(&interfaces, 0, sizeof(interfaces));
    memset(&list, 0, sizeof(list));
    list.ifc_len = sizeof(interfaces);
    list.ifc_req = interfaces;
    if (ioctl(fd, SIOCGIFCONF, &list) != 0) {
        close(fd);
        return;
    }

    int best_score = -1;
    size_t count = (size_t)list.ifc_len / sizeof(struct ifreq);
    for (size_t i = 0; i < count; i++) {
        struct ifreq *candidate = &interfaces[i];
        if (candidate->ifr_addr.sa_family != AF_INET) continue;
        struct ifreq flags_request;
        memset(&flags_request, 0, sizeof(flags_request));
        mp_safe_str(flags_request.ifr_name, sizeof(flags_request.ifr_name), candidate->ifr_name);
        if (ioctl(fd, SIOCGIFFLAGS, &flags_request) != 0) continue;
        if (!(flags_request.ifr_flags & IFF_UP) || (flags_request.ifr_flags & IFF_LOOPBACK)) continue;

        struct sockaddr_in *address = (struct sockaddr_in *)&candidate->ifr_addr;
        char value[INET_ADDRSTRLEN] = "";
        if (!inet_ntop(AF_INET, &address->sin_addr, value, sizeof(value))) continue;
        int score = 10;
        if (preferred[0] && strcmp(candidate->ifr_name, preferred) == 0) score = 100;
        else if (strncmp(candidate->ifr_name, "wl", 2) == 0) score = 70;
        else if (strncmp(candidate->ifr_name, "en", 2) == 0 ||
                 strncmp(candidate->ifr_name, "eth", 3) == 0) score = 50;
        if (score <= best_score) continue;
        best_score = score;
        mp_safe_str(info->interface_name, sizeof(info->interface_name), candidate->ifr_name);
        mp_safe_str(info->ip_address, sizeof(info->ip_address), value);
    }
    close(fd);
}

static void find_wireless_interface(struct network_diagnostics *info) {
    FILE *wireless = fopen("/proc/net/wireless", "r");
    if (!wireless) return;
    char line[512];
    while (fgets(line, sizeof(line), wireless)) {
        char name[IFNAMSIZ] = "";
        if (sscanf(line, " %15[^:]:", name) == 1) {
            if (!info->interface_name[0])
                mp_safe_str(info->interface_name, sizeof(info->interface_name), name);
            break;
        }
    }
    fclose(wireless);
}

static void read_wifi_details(struct network_diagnostics *info) {
    if (!info->interface_name[0]) return;
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd >= 0) {
        struct iwreq request;
        char essid[IW_ESSID_MAX_SIZE + 1];
        memset(&request, 0, sizeof(request));
        memset(essid, 0, sizeof(essid));
        mp_safe_str(request.ifr_name, sizeof(request.ifr_name), info->interface_name);
        request.u.essid.pointer = essid;
        request.u.essid.length = IW_ESSID_MAX_SIZE;
        request.u.essid.flags = 0;
        if (ioctl(fd, SIOCGIWESSID, &request) == 0) {
            size_t length = request.u.essid.length;
            if (length > IW_ESSID_MAX_SIZE) length = IW_ESSID_MAX_SIZE;
            essid[length] = '\0';
            mp_safe_str(info->ssid, sizeof(info->ssid), essid);
        }
        close(fd);
    }

    FILE *wireless = fopen("/proc/net/wireless", "r");
    if (!wireless) return;
    char line[512];
    while (fgets(line, sizeof(line), wireless)) {
        char name[IFNAMSIZ] = "";
        double quality = 0.0;
        double level = 0.0;
        if (sscanf(line, " %15[^:]: %*d %lf %lf", name, &quality, &level) == 3 &&
            strcmp(name, info->interface_name) == 0) {
            int percent = (int)((quality / 70.0) * 100.0 + 0.5);
            if (percent < 0) percent = 0;
            if (percent > 100) percent = 100;
            int dbm = (int)level;
            if (dbm > 100) dbm -= 256;
            info->wifi_signal_percent = percent;
            info->wifi_signal_dbm = dbm;
            info->wifi_signal_available = 1;
            break;
        }
    }
    fclose(wireless);
}

static void collect_network_diagnostics(struct network_diagnostics *info) {
    memset(info, 0, sizeof(*info));
    if (gethostname(info->hostname, sizeof(info->hostname) - 1) != 0)
        mp_safe_str(info->hostname, sizeof(info->hostname), "unknown");
    find_primary_ipv4(info);
    find_wireless_interface(info);
    read_wifi_details(info);

    struct timex clock_state;
    memset(&clock_state, 0, sizeof(clock_state));
    int time_state = adjtimex(&clock_state);
    info->ntp_synchronized = time_state != TIME_ERROR && !(clock_state.status & STA_UNSYNC);
    time_t now = time(NULL);
    struct tm local_time;
    if (localtime_r(&now, &local_time))
        info->system_time_valid = local_time.tm_year + 1900 >= 2024;
}


#define DIAG_MUSIC_DIR "/opt/mk-piclock/assets/music"
#define DIAG_FONT_DIR "/opt/mk-piclock/assets/fonts"
#define DIAG_CONFIG_DIR "/opt/mk-piclock/config"

struct adult_diagnostic_info {
    struct network_diagnostics network;
    double cpu_temperature_c;
    uint64_t storage_free_bytes;
    uint64_t storage_used_bytes;
    uint64_t storage_total_bytes;
    uint64_t music_bytes, music_files;
    uint64_t fonts_bytes, fonts_files;
    uint64_t config_bytes, config_files;
    int api_healthy;
    int core_healthy;
    int oled_ok;
    int touch_ok;
    long long last_successful_alarm;
    char next_alarm_text[128];
    char room_sensor_status[32];
    double room_temperature_c;
    double room_humidity_percent;
    long long room_measured_at;
    char room_sensor_error[192];
    char weather_location[128];
    char weather_warning[192];
    long long weather_observed_at;
    char weather_source_url[MP_WEATHER_SOURCE_URL_MAX];
    char weather_result[64];
    char weather_message[256];
    char weather_run_at[64];
    int weather_status_available;
    char os_pretty_name[256];
    char os_version_id[64];
    char os_codename[64];
    char kernel_release[128];
    char architecture[64];
    char hardware_model[256];
    char pi_serial[64];
    char board_revision[64];
    char machine_id[64];
    char inventory_id[32];
    char cpu_signature[192];
    uint64_t uptime_seconds;
    char root_device[128];
    char root_disk[128];
    char root_filesystem[64];
    char root_mount_options[256];
    int root_read_only;
    char boot_device[128];
    char boot_filesystem[64];
    char boot_mount_point[64];
    int sd_present;
    char sd_device[128];
    char sd_type[32];
    char sd_name[128];
    char sd_manufacturer_id[64];
    char sd_oem_id[64];
    char sd_serial[64];
    char sd_manufacture_date[64];
    char sd_cid[128];
    uint64_t sd_capacity_bytes;
};

static int diag_json_integer(const char *json, const char *key, int fallback) {
    if (!json || !key) return fallback;
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *found = strstr(json, pattern);
    if (!found) return fallback;
    found += strlen(pattern);
    char *end = NULL;
    errno = 0;
    long value = strtol(found, &end, 10);
    return errno || end == found || value < INT_MIN || value > INT_MAX ? fallback : (int)value;
}

static long long diag_json_long_long(const char *json, const char *key, long long fallback) {
    if (!json || !key) return fallback;
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *found = strstr(json, pattern);
    if (!found) return fallback;
    found += strlen(pattern);
    char *end = NULL;
    errno = 0;
    long long value = strtoll(found, &end, 10);
    return errno || end == found ? fallback : value;
}

static double diag_json_double(const char *json, const char *key, double fallback) {
    if (!json || !key) return fallback;
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *found = strstr(json, pattern);
    if (!found) return fallback;
    found += strlen(pattern);
    char *end = NULL;
    errno = 0;
    double value = strtod(found, &end);
    return errno || end == found ? fallback : value;
}

static void diag_json_string(const char *json, const char *key, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (!json || !key) return;
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *found = strstr(json, pattern);
    if (!found) return;
    found += strlen(pattern);
    size_t used = 0;
    while (*found && *found != '"' && used + 1 < out_len) {
        if (*found == '\\' && found[1]) {
            found++;
            if (*found == 'n') out[used++] = '\n';
            else if (*found == 'r') out[used++] = '\r';
            else if (*found == 't') out[used++] = '\t';
            else out[used++] = *found;
            found++;
        } else {
            out[used++] = *found++;
        }
    }
    out[used] = '\0';
}

static void diag_trim(char *text) {
    if (!text) return;
    size_t len = strlen(text);
    while (len && isspace((unsigned char)text[len - 1])) text[--len] = '\0';
    size_t start = 0;
    while (text[start] && isspace((unsigned char)text[start])) start++;
    if (start) memmove(text, text + start, strlen(text + start) + 1);
}

static int diag_read_file(const char *path, char *out, size_t out_len) {
    if (!path || !out || out_len < 2) return -1;
    out[0] = '\0';
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t got = read(fd, out, out_len - 1);
    int saved = errno;
    close(fd);
    if (got < 0) { errno = saved; return -1; }
    out[got] = '\0';
    for (ssize_t i = 0; i < got; i++) if (out[i] == '\0') out[i] = ' ';
    diag_trim(out);
    return out[0] ? 0 : -1;
}

static int diag_os_value(const char *key, char *out, size_t out_len) {
    out[0] = '\0';
    FILE *file = fopen("/etc/os-release", "r");
    if (!file) return -1;
    char line[512];
    size_t key_len = strlen(key);
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, key, key_len) || line[key_len] != '=') continue;
        char *value = line + key_len + 1;
        diag_trim(value);
        size_t len = strlen(value);
        if (len >= 2 && ((value[0] == '"' && value[len - 1] == '"') ||
                         (value[0] == '\'' && value[len - 1] == '\''))) {
            value[len - 1] = '\0'; value++;
        }
        mp_safe_str(out, out_len, value);
        break;
    }
    fclose(file);
    return out[0] ? 0 : -1;
}

static int diag_cpuinfo_value(const char *key, char *out, size_t out_len) {
    out[0] = '\0';
    FILE *file = fopen("/proc/cpuinfo", "r");
    if (!file) return -1;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        diag_trim(line);
        if (strcmp(line, key)) continue;
        diag_trim(colon + 1);
        mp_safe_str(out, out_len, colon + 1);
        break;
    }
    fclose(file);
    return out[0] ? 0 : -1;
}

static void diag_inventory_id(const char *serial, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (!serial || !serial[0]) return;

    char compact[64];
    size_t used = 0;
    for (const unsigned char *c = (const unsigned char *)serial;
         *c && used + 1 < sizeof(compact); c++) {
        if (isalnum(*c)) compact[used++] = (char)toupper(*c);
    }
    compact[used] = '\0';

    if (used >= 8) {
        static const size_t required = sizeof("MK-0000-0000");
        if (out_len < required) return;
        memcpy(out, "MK-", 3);
        memcpy(out + 3, compact + used - 8, 4);
        out[7] = '-';
        memcpy(out + 8, compact + used - 4, 4);
        out[12] = '\0';
        return;
    }

    if (out_len <= 3) return;
    memcpy(out, "MK-", 3);
    size_t copy_len = used;
    if (copy_len > out_len - 4) copy_len = out_len - 4;
    memcpy(out + 3, compact, copy_len);
    out[3 + copy_len] = '\0';
}

static int diag_mount_details(const char *mount_point, char *device, size_t device_len,
                              char *filesystem, size_t filesystem_len,
                              char *options, size_t options_len) {
    FILE *mounts = setmntent("/proc/self/mounts", "r");
    if (!mounts) return -1;
    struct mntent entry; char buffer[4096]; int found = 0;
    while (getmntent_r(mounts, &entry, buffer, sizeof(buffer))) {
        if (strcmp(entry.mnt_dir, mount_point)) continue;
        mp_safe_str(device, device_len, entry.mnt_fsname);
        mp_safe_str(filesystem, filesystem_len, entry.mnt_type);
        mp_safe_str(options, options_len, entry.mnt_opts);
        found = 1; break;
    }
    endmntent(mounts);
    return found ? 0 : -1;
}

static int diag_options_contains(const char *options, const char *wanted) {
    if (!options || !wanted) return 0;
    size_t wanted_len = strlen(wanted);
    for (const char *c = options; *c;) {
        while (*c == ',') c++;
        const char *end = strchr(c, ',');
        size_t len = end ? (size_t)(end - c) : strlen(c);
        if (len == wanted_len && !strncmp(c, wanted, len)) return 1;
        if (!end) break;
        c = end + 1;
    }
    return 0;
}

static int diag_format_device_path(const char *name, char *out, size_t out_len) {
    if (!out || out_len == 0) return -1;
    out[0] = '\0';
    if (!name || !name[0] || strchr(name, '/')) return -1;

    size_t name_len = strlen(name);
    if (out_len <= 5 || name_len > out_len - 6) return -1;
    memcpy(out, "/dev/", 5);
    memcpy(out + 5, name, name_len + 1);
    return 0;
}

static int diag_resolve_mount_device(const char *mount_point, char *out, size_t out_len) {
    struct stat st;
    if (!out || out_len == 0 || stat(mount_point, &st)) return -1;
    out[0] = '\0';

    char sys_path[128], target[PATH_MAX];
    int written = snprintf(sys_path, sizeof(sys_path), "/sys/dev/block/%u:%u",
                           major(st.st_dev), minor(st.st_dev));
    if (written < 0 || (size_t)written >= sizeof(sys_path)) return -1;

    ssize_t len = readlink(sys_path, target, sizeof(target) - 1);
    if (len <= 0) return -1;
    target[len] = '\0';

    const char *name = strrchr(target, '/');
    name = name ? name + 1 : target;
    return diag_format_device_path(name, out, out_len);
}

static void diag_parent_device(const char *device, char *out, size_t out_len) {
    out[0] = '\0';
    if (!device || strncmp(device, "/dev/", 5)) return;
    char name[128]; mp_safe_str(name, sizeof(name), device + 5);
    size_t len = strlen(name);
    if (!strncmp(name, "mmcblk", 6) || !strncmp(name, "nvme", 4)) {
        char *p = strrchr(name, 'p');
        if (p && p[1] && strspn(p + 1, "0123456789") == strlen(p + 1)) *p = '\0';
    } else while (len && isdigit((unsigned char)name[len - 1])) name[--len] = '\0';
    if (name[0]) (void)diag_format_device_path(name, out, out_len);
}

static uint64_t diag_read_uint64(const char *path) {
    char text[64];
    if (diag_read_file(path, text, sizeof(text))) return 0;
    char *end = NULL; errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    return errno || end == text ? 0 : (uint64_t)value;
}

static void diag_directory_usage_fd(int fd, unsigned depth, uint64_t *bytes, uint64_t *files) {
    if (fd < 0 || depth > 32) { if (fd >= 0) close(fd); return; }
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); return; }
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat st;
        if (fstatat(dirfd(dir), entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) continue;
        if (S_ISREG(st.st_mode)) {
            if (st.st_size > 0 && (uint64_t)st.st_size <= UINT64_MAX - *bytes) *bytes += (uint64_t)st.st_size;
            if (*files < UINT64_MAX) (*files)++;
        } else if (S_ISDIR(st.st_mode)) {
            int child = openat(dirfd(dir), entry->d_name, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
            if (child >= 0) diag_directory_usage_fd(child, depth + 1, bytes, files);
        }
    }
    closedir(dir);
}

static void diag_directory_usage(const char *path, uint64_t *bytes, uint64_t *files) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (fd >= 0) diag_directory_usage_fd(fd, 0, bytes, files);
}

static void diag_read_platform(struct adult_diagnostic_info *info) {
    (void)diag_os_value("PRETTY_NAME", info->os_pretty_name, sizeof(info->os_pretty_name));
    (void)diag_os_value("VERSION_ID", info->os_version_id, sizeof(info->os_version_id));
    (void)diag_os_value("VERSION_CODENAME", info->os_codename, sizeof(info->os_codename));
    struct utsname identity;
    if (!uname(&identity)) {
        mp_safe_str(info->kernel_release, sizeof(info->kernel_release), identity.release);
        mp_safe_str(info->architecture, sizeof(info->architecture), identity.machine);
    }
    if (diag_read_file("/proc/device-tree/model", info->hardware_model, sizeof(info->hardware_model)))
        (void)diag_read_file("/sys/firmware/devicetree/base/model", info->hardware_model, sizeof(info->hardware_model));
    if (diag_read_file("/sys/firmware/devicetree/base/serial-number", info->pi_serial, sizeof(info->pi_serial)) &&
        diag_read_file("/proc/device-tree/serial-number", info->pi_serial, sizeof(info->pi_serial)))
        (void)diag_cpuinfo_value("Serial", info->pi_serial, sizeof(info->pi_serial));
    if (!strncmp(info->pi_serial, "0x", 2) || !strncmp(info->pi_serial, "0X", 2))
        memmove(info->pi_serial, info->pi_serial + 2, strlen(info->pi_serial + 2) + 1);
    (void)diag_cpuinfo_value("Revision", info->board_revision, sizeof(info->board_revision));
    (void)diag_read_file("/etc/machine-id", info->machine_id, sizeof(info->machine_id));
    diag_inventory_id(info->pi_serial, info->inventory_id, sizeof(info->inventory_id));
    char impl[32]="", arch[32]="", variant[32]="", part[32]="", revision[32]="";
    (void)diag_cpuinfo_value("CPU implementer", impl, sizeof(impl));
    (void)diag_cpuinfo_value("CPU architecture", arch, sizeof(arch));
    (void)diag_cpuinfo_value("CPU variant", variant, sizeof(variant));
    (void)diag_cpuinfo_value("CPU part", part, sizeof(part));
    (void)diag_cpuinfo_value("CPU revision", revision, sizeof(revision));
    if (impl[0] || arch[0] || variant[0] || part[0] || revision[0])
        snprintf(info->cpu_signature, sizeof(info->cpu_signature),
                 "implementer %s / architecture %s / variant %s / part %s / revision %s",
                 impl[0]?impl:"unknown", arch[0]?arch:"unknown", variant[0]?variant:"unknown",
                 part[0]?part:"unknown", revision[0]?revision:"unknown");
    char uptime[128];
    if (!diag_read_file("/proc/uptime", uptime, sizeof(uptime))) {
        char *end = NULL; double seconds = strtod(uptime, &end);
        if (end != uptime && seconds > 0) info->uptime_seconds = (uint64_t)seconds;
    }
}

static int diag_primary_mmc_name(const char *name) {
    if (!name || strncmp(name, "mmcblk", 6)) return 0;
    const char *c = name + 6;
    if (!isdigit((unsigned char)*c)) return 0;
    while (isdigit((unsigned char)*c)) c++;
    return *c == '\0';
}

static void diag_read_storage(struct adult_diagnostic_info *info) {
    char options[256]="";
    if (!diag_mount_details("/", info->root_device, sizeof(info->root_device), info->root_filesystem,
                            sizeof(info->root_filesystem), options, sizeof(options))) {
        mp_safe_str(info->root_mount_options, sizeof(info->root_mount_options), options);
        info->root_read_only = diag_options_contains(options, "ro");
        if (strncmp(info->root_device, "/dev/", 5) != 0 || !strcmp(info->root_device, "/dev/root")) {
            char resolved[128]="";
            if (!diag_resolve_mount_device("/", resolved, sizeof(resolved)))
                mp_safe_str(info->root_device, sizeof(info->root_device), resolved);
        }
        diag_parent_device(info->root_device, info->root_disk, sizeof(info->root_disk));
    }
    char boot_options[256]="";
    if (!diag_mount_details("/boot/firmware", info->boot_device, sizeof(info->boot_device),
                            info->boot_filesystem, sizeof(info->boot_filesystem), boot_options, sizeof(boot_options)))
        mp_safe_str(info->boot_mount_point, sizeof(info->boot_mount_point), "/boot/firmware");
    else if (!diag_mount_details("/boot", info->boot_device, sizeof(info->boot_device),
                                 info->boot_filesystem, sizeof(info->boot_filesystem), boot_options, sizeof(boot_options)))
        mp_safe_str(info->boot_mount_point, sizeof(info->boot_mount_point), "/boot");
    char block[64]="";
    if (!strncmp(info->root_disk, "/dev/mmcblk", 11)) mp_safe_str(block, sizeof(block), info->root_disk + 5);
    if (!block[0]) {
        DIR *dir = opendir("/sys/class/block");
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir))) if (diag_primary_mmc_name(entry->d_name)) { mp_safe_str(block, sizeof(block), entry->d_name); break; }
            closedir(dir);
        }
    }
    if (!block[0]) return;
    if (diag_format_device_path(block, info->sd_device, sizeof(info->sd_device)) != 0) return;
    info->sd_present = 1;
    char path[PATH_MAX];
#define DIAG_SD(field,target) do { snprintf(path,sizeof(path),"/sys/class/block/%s/device/%s",block,field); (void)diag_read_file(path,target,sizeof(target)); } while(0)
    DIAG_SD("type", info->sd_type); DIAG_SD("name", info->sd_name);
    DIAG_SD("manfid", info->sd_manufacturer_id); DIAG_SD("oemid", info->sd_oem_id);
    DIAG_SD("serial", info->sd_serial); DIAG_SD("date", info->sd_manufacture_date); DIAG_SD("cid", info->sd_cid);
#undef DIAG_SD
    snprintf(path, sizeof(path), "/sys/class/block/%s/size", block);
    uint64_t sectors = diag_read_uint64(path);
    if (sectors <= UINT64_MAX / 512ULL) info->sd_capacity_bytes = sectors * 512ULL;
}

static void diag_read_core(struct adult_diagnostic_info *info) {
    struct ipc_result ping;
    if (!ipc_call(MP_IPC_OP_PING, NULL, 0, &ping)) {
        info->core_healthy = ping.status >= 200 && ping.status < 300;
        ipc_result_free(&ping);
    }
    struct ipc_result status;
    if (ipc_call(MP_IPC_OP_STATUS, NULL, 0, &status)) return;
    if (status.status == 200 && status.body && status.body_len < MP_IPC_MAX_PAYLOAD) {
        char *json = malloc(status.body_len + 1);
        if (json) {
            memcpy(json, status.body, status.body_len); json[status.body_len] = '\0';
            info->oled_ok = diag_json_integer(json, "oled_ok", 0);
            info->touch_ok = diag_json_integer(json, "touch_ok", 0);
            info->last_successful_alarm = diag_json_long_long(json, "last_successful_alarm", 0);
            diag_json_string(json, "next_alarm_text", info->next_alarm_text, sizeof(info->next_alarm_text));
            const char *room = strstr(json, "\"room_sensor\":{");
            if (room) {
                diag_json_string(room, "status", info->room_sensor_status, sizeof(info->room_sensor_status));
                info->room_temperature_c = diag_json_double(room, "temperature_c", 0.0);
                info->room_humidity_percent = diag_json_double(room, "humidity_percent", 0.0);
                info->room_measured_at = diag_json_long_long(room, "measured_at", 0);
                diag_json_string(room, "error", info->room_sensor_error, sizeof(info->room_sensor_error));
            }
            const char *weather = strstr(json, "\"weather\":{");
            if (weather) {
                diag_json_string(weather, "location", info->weather_location, sizeof(info->weather_location));
                diag_json_string(weather, "warning_type", info->weather_warning, sizeof(info->weather_warning));
                info->weather_observed_at = diag_json_long_long(weather, "observed_at", 0);
            }
            free(json);
        }
    }
    ipc_result_free(&status);
}

static void diag_read_weather(struct adult_diagnostic_info *info) {
    char error[192]="";
    if (mp_weather_source_read(info->weather_source_url, sizeof(info->weather_source_url), error, sizeof(error)))
        mp_safe_str(info->weather_source_url, sizeof(info->weather_source_url), "Unavailable");
    char status[MP_WEATHER_STATUS_JSON_MAX];
    if (!mp_weather_status_read_json(status, sizeof(status), error, sizeof(error))) {
        info->weather_status_available = 1;
        diag_json_string(status, "result", info->weather_result, sizeof(info->weather_result));
        diag_json_string(status, "message", info->weather_message, sizeof(info->weather_message));
        diag_json_string(status, "run_at", info->weather_run_at, sizeof(info->weather_run_at));
    } else {
        mp_safe_str(info->weather_message, sizeof(info->weather_message), error);
    }
}

static void collect_adult_diagnostics(struct adult_diagnostic_info *info) {
    memset(info, 0, sizeof(*info));
    info->api_healthy = 1;
    collect_network_diagnostics(&info->network);
    FILE *temperature = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    long millidegrees = 0;
    if (temperature) {
        if (fscanf(temperature, "%ld", &millidegrees) == 1) info->cpu_temperature_c = (double)millidegrees / 1000.0;
        fclose(temperature);
    }
    struct statvfs storage;
    if (!statvfs("/", &storage)) {
        uint64_t block = (uint64_t)storage.f_frsize;
        uint64_t total = (uint64_t)storage.f_blocks;
        uint64_t free_all = (uint64_t)storage.f_bfree;
        info->storage_free_bytes = (uint64_t)storage.f_bavail * block;
        info->storage_used_bytes = total >= free_all ? (total - free_all) * block : 0;
        info->storage_total_bytes = total * block;
    }
    diag_directory_usage(DIAG_MUSIC_DIR, &info->music_bytes, &info->music_files);
    diag_directory_usage(DIAG_FONT_DIR, &info->fonts_bytes, &info->fonts_files);
    diag_directory_usage(DIAG_CONFIG_DIR, &info->config_bytes, &info->config_files);
    diag_read_platform(info);
    diag_read_storage(info);
    diag_read_core(info);
    diag_read_weather(info);
}

static void diag_append_json_string(struct mp_buffer *body, const char *name, const char *value) {
    mp_buffer_appendf(body, ",\"%s\":\"", name);
    mp_buffer_append_json_string(body, value ? value : "");
    mp_buffer_append(body, "\"");
}

static int request_origin_allowed(struct MHD_Connection *connection) {
    const char *origin = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Origin");
    return origin && g_allowed_origin[0] && strcmp(origin, g_allowed_origin) == 0;
}

static size_t route_upload_limit(const struct api_route *route) {
    if (!route) return 0;
    switch (route->id) {
        case ROUTE_UPLOAD_MUSIC:
            return MP_MUSIC_UPLOAD_MAX_BYTES;
        case ROUTE_UPLOAD_FONT:
            return MP_FONT_UPLOAD_MAX_BYTES;
        case ROUTE_BACKUP_RESTORE:
            return (size_t)MAX_BACKUP_BYTES;
        default:
            return 0;
    }
}

static const struct api_route *find_route(const char *method, const char *path) {
    for (size_t i = 0; i < sizeof(g_routes) / sizeof(g_routes[0]); i++) {
        if (strcmp(method, g_routes[i].method) == 0 && strcmp(path, g_routes[i].path) == 0)
            return &g_routes[i];
    }
    return NULL;
}

static enum MHD_Result add_header(struct MHD_Response *response, const char *name, const char *value) {
    return MHD_add_response_header(response, name, value);
}

static void add_api_headers(struct MHD_Connection *connection, struct MHD_Response *response) {
    (void)add_header(response, "Cache-Control", "no-store");
    if (request_origin_allowed(connection)) {
        (void)add_header(response, "Access-Control-Allow-Origin", g_allowed_origin);
        (void)add_header(response, "Vary", "Origin");
    }
    (void)add_header(response, "X-MK-PICLOCK-API-Version", API_VERSION);
    (void)add_header(response, "X-Content-Type-Options", "nosniff");
    (void)add_header(response, "Referrer-Policy", "no-referrer");
}

static enum MHD_Result queue_buffer(struct MHD_Connection *connection, unsigned int status,
                                    const char *content_type, void *body, size_t body_len,
                                    enum MHD_ResponseMemoryMode mode, int api_headers) {
    struct MHD_Response *response = MHD_create_response_from_buffer(body_len, body, mode);
    if (!response) {
        if (mode == MHD_RESPMEM_MUST_FREE) free(body);
        return MHD_NO;
    }
    if (content_type) (void)add_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, content_type);
    if (api_headers) add_api_headers(connection, response);
    enum MHD_Result result = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return result;
}

static enum MHD_Result queue_json(struct MHD_Connection *connection, unsigned int status, const char *json) {
    const char *body = json ? json : "{}";
    return queue_buffer(connection, status, "application/json; charset=utf-8",
                        (void *)body, strlen(body), MHD_RESPMEM_MUST_COPY, 1);
}

static enum MHD_Result queue_json_builder(struct MHD_Connection *connection, unsigned int status,
                                          struct mp_buffer *buffer) {
    size_t length = 0;
    char *body = mp_buffer_steal(buffer, &length);
    mp_buffer_free(buffer);
    if (!body) return queue_json(connection, 500, "{\"ok\":false,\"error\":\"JSON response exceeded its limit\"}");
    return queue_buffer(connection, status, "application/json; charset=utf-8",
                        body, length, MHD_RESPMEM_MUST_FREE, 1);
}

static enum MHD_Result queue_options(struct MHD_Connection *connection) {
    if (!request_origin_allowed(connection))
        return queue_json(connection, MHD_HTTP_FORBIDDEN,
                          "{\"ok\":false,\"error\":\"cross-origin access is disabled\"}");
    struct MHD_Response *response = MHD_create_response_from_buffer(0, (void *)"", MHD_RESPMEM_PERSISTENT);
    if (!response) return MHD_NO;
    add_api_headers(connection, response);
    (void)add_header(response, "Access-Control-Allow-Headers", "Content-Type, Accept");
    (void)add_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    (void)add_header(response, "Access-Control-Max-Age", "600");
    enum MHD_Result result = MHD_queue_response(connection, MHD_HTTP_NO_CONTENT, response);
    MHD_destroy_response(response);
    return result;
}

static const char *content_type_for_path(const char *path) {
    const char *dot = strrchr(path ? path : "", '.');
    if (!dot) return "application/octet-stream";
    if (strcasecmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(dot, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcasecmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(dot, ".webmanifest") == 0) return "application/manifest+json; charset=utf-8";
    if (strcasecmp(dot, ".png") == 0) return "image/png";
    if (strcasecmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(dot, ".ico") == 0) return "image/x-icon";
    if (strcasecmp(dot, ".ttf") == 0) return "font/ttf";
    if (strcasecmp(dot, ".otf") == 0) return "font/otf";
    return "application/octet-stream";
}

static enum MHD_Result queue_file(struct MHD_Connection *connection, const char *path,
                                  const char *content_type, int api_headers,
                                  unsigned int cache_seconds) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return MHD_NO;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > MAX_STATIC_FILE) {
        close(fd);
        return MHD_NO;
    }

    char etag[80];
    snprintf(etag, sizeof(etag), "\"%llx-%llx\"",
             (unsigned long long)st.st_mtime, (unsigned long long)st.st_size);
    const char *client_etag = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "If-None-Match");
    if (cache_seconds > 0 && client_etag && strcmp(client_etag, etag) == 0) {
        close(fd);
        struct MHD_Response *not_modified = MHD_create_response_from_buffer(0, (void *)"", MHD_RESPMEM_PERSISTENT);
        if (!not_modified) return MHD_NO;
        (void)add_header(not_modified, "ETag", etag);
        if (api_headers) add_api_headers(connection, not_modified);
        else {
            char cache[64];
            snprintf(cache, sizeof(cache), "public, max-age=%u", cache_seconds);
            (void)add_header(not_modified, "Cache-Control", cache);
        }
        enum MHD_Result queued = MHD_queue_response(connection, MHD_HTTP_NOT_MODIFIED, not_modified);
        MHD_destroy_response(not_modified);
        return queued;
    }

    struct MHD_Response *response = MHD_create_response_from_fd64((uint64_t)st.st_size, fd);
    if (!response) {
        close(fd);
        return MHD_NO;
    }
    (void)add_header(response, MHD_HTTP_HEADER_CONTENT_TYPE,
                     content_type ? content_type : content_type_for_path(path));
    (void)add_header(response, "ETag", etag);
    char modified[64];
    struct tm tmv;
    if (gmtime_r(&st.st_mtime, &tmv) &&
        strftime(modified, sizeof(modified), "%a, %d %b %Y %H:%M:%S GMT", &tmv) > 0)
        (void)add_header(response, "Last-Modified", modified);
    if (api_headers) add_api_headers(connection, response);
    else {
        if (cache_seconds == 0) {
            (void)add_header(response, "Cache-Control", "no-store");
        } else {
            char cache[64];
            snprintf(cache, sizeof(cache), "public, max-age=%u", cache_seconds);
            (void)add_header(response, "Cache-Control", cache);
        }
        (void)add_header(response, "X-Content-Type-Options", "nosniff");
    }
    enum MHD_Result result = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return result;
}


static int path_has_encoded_separator_or_dot(const char *path) {
    for (const char *p = path; p && *p; p++) {
        if (*p != '%' || !p[1] || !p[2]) continue;
        char a = (char)tolower((unsigned char)p[1]);
        char b = (char)tolower((unsigned char)p[2]);
        if ((a == '2' && (b == 'e' || b == 'f')) || (a == '5' && b == 'c')) return 1;
    }
    return 0;
}

static int safe_web_asset_path(const char *path) {
    if (!path || path[0] != '/' || strstr(path, "..") || strchr(path, '\\')) return 0;
    if (path_has_encoded_separator_or_dot(path)) return 0;
    return strncmp(path, "/assets/", 8) == 0 || strncmp(path, "/modules/", 9) == 0;
}

static enum MHD_Result serve_static(struct MHD_Connection *connection, const char *method,
                                    const char *path, int *handled) {
    *handled = 0;
    if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) return MHD_YES;
    char full[2048];
    if (safe_web_asset_path(path)) {
        *handled = 1;
        int n = snprintf(full, sizeof(full), "%s%s", WEB_DIR, path);
        if (n <= 0 || (size_t)n >= sizeof(full))
            return queue_json(connection, MHD_HTTP_URI_TOO_LONG,
                              "{\"ok\":false,\"error\":\"asset path too long\"}");
        enum MHD_Result result = queue_file(connection, full, NULL, 0,
                                            0);
        return result == MHD_NO ? queue_json(connection, 404, "{\"ok\":false,\"error\":\"asset not found\"}") : result;
    }
    if (strcmp(path, "/favicon.ico") == 0) {
        *handled = 1;
        snprintf(full, sizeof(full), "%s/favicon.ico", WEB_DIR);
        enum MHD_Result result = queue_file(connection, full, "image/x-icon", 0, 0);
        return result == MHD_NO ? queue_json(connection, 404, "{\"ok\":false,\"error\":\"favicon not found\"}") : result;
    }
    if (strcmp(path, "/api/v1/openapi.json") == 0) {
        *handled = 1;
        snprintf(full, sizeof(full), "%s/openapi-v1.json", API_DOC_DIR);
        enum MHD_Result result = queue_file(connection, full, "application/json; charset=utf-8", 1, 0);
        return result == MHD_NO ? queue_json(connection, 404, "{\"ok\":false,\"error\":\"OpenAPI file not found\"}") : result;
    }
    if (strcmp(path, "/") == 0) {
        *handled = 1;
        snprintf(full, sizeof(full), "%s/index.html", WEB_DIR);
        enum MHD_Result result = queue_file(connection, full, "text/html; charset=utf-8", 0, 0);
        return result == MHD_NO ? queue_json(connection, 404, "{\"ok\":false,\"error\":\"GUI not installed\"}") : result;
    }
    return MHD_YES;
}

static int connect_core(void) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct timeval tv = {.tv_sec = SOCKET_TIMEOUT_SEC, .tv_usec = 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int socket_buffer = (int)(MP_IPC_MAX_PAYLOAD * 2u);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &socket_buffer, sizeof(socket_buffer));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &socket_buffer, sizeof(socket_buffer));
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    mp_safe_str(addr.sun_path, sizeof(addr.sun_path), CORE_SOCKET_PATH);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
#ifdef SO_PEERCRED
    struct ucred credentials;
    socklen_t credentials_len = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &credentials_len) != 0 ||
        (g_expected_core_uid != (uid_t)-1 && credentials.uid != g_expected_core_uid && credentials.uid != 0)) {
        close(fd);
        errno = EPERM;
        return -1;
    }
#endif
    return fd;
}

static int ipc_call(uint16_t opcode, const void *payload, size_t payload_len, struct ipc_result *result) {
    memset(result, 0, sizeof(*result));
    if (payload_len > MP_IPC_MAX_PAYLOAD) return -1;
    int fd = connect_core();
    if (fd < 0) return -1;
    struct mp_ipc_request_header request = {
        .magic = MP_IPC_MAGIC,
        .version = MP_IPC_VERSION,
        .opcode = opcode,
        .payload_len = (uint32_t)payload_len
    };
    if (mp_send_packet(fd, &request, sizeof(request), payload, payload_len) != 0) {
        close(fd);
        return -1;
    }
    void *packet = NULL;
    size_t packet_len = 0;
    if (mp_recv_packet_alloc(fd, &packet,
                             sizeof(struct mp_ipc_response_header) + MP_IPC_MAX_PAYLOAD,
                             &packet_len) != 0) {
        close(fd);
        return -1;
    }
    close(fd);
    if (packet_len < sizeof(struct mp_ipc_response_header)) {
        free(packet);
        return -1;
    }
    struct mp_ipc_response_header response;
    memcpy(&response, packet, sizeof(response));
    if (response.magic != MP_IPC_MAGIC || response.version != MP_IPC_VERSION ||
        response.body_len > MP_IPC_MAX_PAYLOAD ||
        packet_len != sizeof(response) + response.body_len) {
        free(packet);
        return -1;
    }
    unsigned char *body = NULL;
    if (response.body_len) {
        body = malloc(response.body_len);
        if (!body) {
            free(packet);
            return -1;
        }
        memcpy(body, (unsigned char *)packet + sizeof(response), response.body_len);
    }
    free(packet);
    result->status = response.status;
    result->content_type = response.content_type;
    result->body = body;
    result->body_len = response.body_len;
    return 0;
}

static void ipc_result_free(struct ipc_result *result) {
    if (!result) return;
    free(result->body);
    memset(result, 0, sizeof(*result));
}

static enum MHD_Result queue_ipc_result(struct MHD_Connection *connection, struct ipc_result *result) {
    const char *type = result->content_type == MP_IPC_CONTENT_JSON
        ? "application/json; charset=utf-8"
        : result->content_type == MP_IPC_CONTENT_TEXT ? "text/plain; charset=utf-8" : "application/octet-stream";
    unsigned char *body = result->body;
    size_t len = result->body_len;
    result->body = NULL;
    return queue_buffer(connection, result->status, type,
                        body ? body : (void *)"", len,
                        body ? MHD_RESPMEM_MUST_FREE : MHD_RESPMEM_PERSISTENT, 1);
}

static enum MHD_Result call_core(struct MHD_Connection *connection, uint16_t opcode,
                                 const void *payload, size_t payload_len) {
    struct ipc_result result;
    if (ipc_call(opcode, payload, payload_len, &result) != 0)
        return queue_json(connection, MHD_HTTP_SERVICE_UNAVAILABLE,
                          "{\"ok\":false,\"error\":\"clock core unavailable\"}");
    enum MHD_Result queued = queue_ipc_result(connection, &result);
    ipc_result_free(&result);
    return queued;
}

static int get_asset_state(struct mp_ipc_asset_state *state) {
    struct ipc_result result;
    if (ipc_call(MP_IPC_OP_ASSET_STATE, NULL, 0, &result) != 0) return -1;
    int ok = result.status == 200 && result.content_type == MP_IPC_CONTENT_BINARY &&
             result.body_len == sizeof(*state);
    if (ok) memcpy(state, result.body, sizeof(*state));
    ipc_result_free(&result);
    return ok ? 0 : -1;
}

static int notify_asset(uint8_t kind, uint8_t action, uint32_t count, const char *file) {
    struct mp_ipc_asset_event event;
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.action = action;
    event.count = count;
    mp_safe_str(event.file, sizeof(event.file), file);
    struct ipc_result result;
    if (ipc_call(MP_IPC_OP_ASSET_EVENT, &event, sizeof(event), &result) != 0) return -1;
    int ok = result.status >= 200 && result.status < 300;
    ipc_result_free(&result);
    return ok ? 0 : -1;
}

static int notify_processed_music(const char *file, void *userdata) {
    (void)userdata;
    return notify_asset(MP_IPC_ASSET_MUSIC, MP_IPC_ASSET_UPLOADED, 1, file);
}

static pthread_mutex_t g_maintenance_lock = PTHREAD_MUTEX_INITIALIZER;

static int read_fd_full(int fd, void *buffer, size_t length) {
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


struct zip_entry {
    char name[512];
    uint32_t crc32;
    uint32_t size;
    uint32_t local_offset;
    uint16_t dos_time;
    uint16_t dos_date;
};

static int write_u16(FILE *file, uint16_t value) {
    unsigned char bytes[2] = {(unsigned char)(value & 0xffu), (unsigned char)((value >> 8) & 0xffu)};
    return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes) ? 0 : -1;
}

static int write_u32(FILE *file, uint32_t value) {
    unsigned char bytes[4] = {
        (unsigned char)(value & 0xffu),
        (unsigned char)((value >> 8) & 0xffu),
        (unsigned char)((value >> 16) & 0xffu),
        (unsigned char)((value >> 24) & 0xffu)
    };
    return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes) ? 0 : -1;
}

static uint32_t g_crc32_table[256];
static pthread_once_t g_crc32_once = PTHREAD_ONCE_INIT;

static void crc32_init_table(void) {
    for (uint32_t value = 0; value < 256; value++) {
        uint32_t crc = value;
        for (unsigned int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
        g_crc32_table[value] = crc;
    }
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *data, size_t length) {
    if (!data && length != 0) return crc;
    (void)pthread_once(&g_crc32_once, crc32_init_table);
    crc = ~crc;
    for (size_t i = 0; i < length; i++)
        crc = g_crc32_table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    return ~crc;
}

static void zip_dos_datetime(time_t value, uint16_t *dos_time, uint16_t *dos_date) {
    struct tm tmv;
    if (!gmtime_r(&value, &tmv)) memset(&tmv, 0, sizeof(tmv));
    int year = tmv.tm_year + 1900;
    if (year < 1980) year = 1980;
    if (year > 2107) year = 2107;
    int month = tmv.tm_mon + 1;
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    int day = tmv.tm_mday;
    if (day < 1) day = 1;
    if (day > 31) day = 31;
    *dos_time = (uint16_t)(((tmv.tm_hour & 31) << 11) | ((tmv.tm_min & 63) << 5) | ((tmv.tm_sec / 2) & 31));
    *dos_date = (uint16_t)(((year - 1980) << 9) | ((month & 15) << 5) | (day & 31));
}

static int copy_memory_to_zip(FILE *zip, const void *data, size_t size, const char *name,
                              struct zip_entry *entry) {
    if (!zip || !data || !name || !entry || size > UINT32_MAX || strlen(name) > UINT16_MAX) return -1;
    memset(entry, 0, sizeof(*entry));
    mp_safe_str(entry->name, sizeof(entry->name), name);
    entry->size = (uint32_t)size;
    entry->crc32 = crc32_update(0, data, size);
    zip_dos_datetime(time(NULL), &entry->dos_time, &entry->dos_date);
    off_t offset = ftello(zip);
    if (offset < 0 || (uint64_t)offset > UINT32_MAX) return -1;
    entry->local_offset = (uint32_t)offset;
    size_t name_len = strlen(entry->name);
    if (write_u32(zip, 0x04034b50u) != 0 || write_u16(zip, 20) != 0 ||
        write_u16(zip, 0) != 0 || write_u16(zip, 0) != 0 ||
        write_u16(zip, entry->dos_time) != 0 || write_u16(zip, entry->dos_date) != 0 ||
        write_u32(zip, entry->crc32) != 0 || write_u32(zip, entry->size) != 0 ||
        write_u32(zip, entry->size) != 0 || write_u16(zip, (uint16_t)name_len) != 0 ||
        write_u16(zip, 0) != 0 || fwrite(entry->name, 1, name_len, zip) != name_len ||
        fwrite(data, 1, size, zip) != size) return -1;
    return 0;
}

static int copy_file_to_zip(FILE *zip, const char *path, const char *name,
                            struct zip_entry *entry) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > UINT32_MAX) {
        close(fd);
        return -1;
    }
    unsigned char *data = malloc((size_t)st.st_size ? (size_t)st.st_size : 1u);
    if (!data) { close(fd); return -1; }
    int ok = st.st_size == 0 || read_fd_full(fd, data, (size_t)st.st_size) == 0;
    close(fd);
    if (!ok) { free(data); return -1; }
    int result = copy_memory_to_zip(zip, data, (size_t)st.st_size, name, entry);
    free(data);
    return result;
}

static int finish_zip(FILE *zip, struct zip_entry *entries, int count) {
    if (!zip || !entries || count < 0 || count > UINT16_MAX) return -1;
    off_t central_offset_value = ftello(zip);
    if (central_offset_value < 0 || (uint64_t)central_offset_value > UINT32_MAX) return -1;
    uint32_t central_offset = (uint32_t)central_offset_value;
    for (int i = 0; i < count; i++) {
        size_t name_len = strlen(entries[i].name);
        if (write_u32(zip, 0x02014b50u) != 0 || write_u16(zip, 20) != 0 ||
            write_u16(zip, 20) != 0 || write_u16(zip, 0) != 0 || write_u16(zip, 0) != 0 ||
            write_u16(zip, entries[i].dos_time) != 0 || write_u16(zip, entries[i].dos_date) != 0 ||
            write_u32(zip, entries[i].crc32) != 0 || write_u32(zip, entries[i].size) != 0 ||
            write_u32(zip, entries[i].size) != 0 || write_u16(zip, (uint16_t)name_len) != 0 ||
            write_u16(zip, 0) != 0 || write_u16(zip, 0) != 0 || write_u16(zip, 0) != 0 ||
            write_u16(zip, 0) != 0 || write_u32(zip, 0100644u << 16) != 0 ||
            write_u32(zip, entries[i].local_offset) != 0 ||
            fwrite(entries[i].name, 1, name_len, zip) != name_len) return -1;
    }
    off_t end_value = ftello(zip);
    if (end_value < 0 || (uint64_t)end_value > UINT32_MAX) return -1;
    uint32_t central_size = (uint32_t)end_value - central_offset;
    if (write_u32(zip, 0x06054b50u) != 0 || write_u16(zip, 0) != 0 ||
        write_u16(zip, 0) != 0 || write_u16(zip, (uint16_t)count) != 0 ||
        write_u16(zip, (uint16_t)count) != 0 || write_u32(zip, central_size) != 0 ||
        write_u32(zip, central_offset) != 0 || write_u16(zip, 0) != 0 ||
        fflush(zip) != 0 || fsync(fileno(zip)) != 0) return -1;
    return 0;
}

static int backup_font_allowed(const char *name) {
    return mp_asset_safe_filename(name) && name[0] != '.' && mp_asset_has_font_ext(name);
}

static int add_fonts_to_backup(FILE *zip, struct zip_entry *entries, int *count,
                               uint64_t *total_bytes) {
    DIR *dir = opendir(MP_FONT_DIR);
    if (!dir) return errno == ENOENT ? 0 : -1;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL) {
        if (!backup_font_allowed(item->d_name)) continue;
        if (*count >= BACKUP_ENTRY_MAX) { closedir(dir); errno = EFBIG; return -1; }
        char path[PATH_MAX], archive_name[512];
        if (snprintf(path, sizeof(path), "%s/%s", MP_FONT_DIR, item->d_name) >= (int)sizeof(path) ||
            snprintf(archive_name, sizeof(archive_name), "assets/fonts/%s", item->d_name) >= (int)sizeof(archive_name))
            continue;
        struct stat st;
        if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) continue;
        if ((uint64_t)st.st_size > MAX_BACKUP_BYTES - *total_bytes) {
            closedir(dir); errno = EFBIG; return -1;
        }
        *total_bytes += (uint64_t)st.st_size;
        if (copy_file_to_zip(zip, path, archive_name, &entries[*count]) != 0) {
            closedir(dir); return -1;
        }
        (*count)++;
    }
    return closedir(dir) == 0 ? 0 : -1;
}

static int build_adult_backup(const char *output_path) {
    struct ipc_result config;
    memset(&config, 0, sizeof(config));
    if (ipc_call(MP_IPC_OP_CONFIG_EXPORT, NULL, 0, &config) != 0 || config.status != 200 ||
        config.content_type != MP_IPC_CONTENT_TEXT || config.body_len == 0 ||
        config.body_len > MP_IPC_CONFIG_MAX_BYTES) {
        ipc_result_free(&config);
        return -1;
    }
    char weather_source[MP_WEATHER_SOURCE_URL_MAX];
    char error[192];
    if (mp_weather_source_read(weather_source, sizeof(weather_source), error, sizeof(error)) != 0) {
        ipc_result_free(&config);
        return -1;
    }
    struct mp_weather_frames_config frames;
    char frames_text[384];
    if (mp_weather_frames_read(&frames, error, sizeof(error)) != 0 ||
        mp_weather_frames_serialize(&frames, frames_text, sizeof(frames_text), error, sizeof(error)) < 0) {
        ipc_result_free(&config);
        return -1;
    }
    size_t source_len = strlen(weather_source);
    if (source_len + 1 >= sizeof(weather_source)) { ipc_result_free(&config); return -1; }
    weather_source[source_len++] = '\n';
    weather_source[source_len] = '\0';

    struct zip_entry *entries = calloc(BACKUP_ENTRY_MAX, sizeof(*entries));
    if (!entries) { ipc_result_free(&config); return -1; }
    FILE *zip = fopen(output_path, "wb");
    if (!zip) { free(entries); ipc_result_free(&config); return -1; }
    int count = 0;
    uint64_t total_bytes = config.body_len + source_len + strlen(frames_text);
    char metadata[512];
    int metadata_len = snprintf(metadata, sizeof(metadata),
        "{\n  \"product\": \"mk-clock-adult\",\n  \"version\": \"%s\",\n  \"api_version\": \"%s\",\n  \"created_at\": %lld,\n  \"format\": 1\n}\n",
        PRODUCT_VERSION, API_VERSION, (long long)time(NULL));
    int failed = total_bytes > MAX_BACKUP_BYTES || metadata_len <= 0 ||
        copy_memory_to_zip(zip, metadata, (size_t)metadata_len, "backup.json", &entries[count++]) != 0 ||
        copy_memory_to_zip(zip, config.body, config.body_len, "config/clock.conf", &entries[count++]) != 0 ||
        copy_memory_to_zip(zip, weather_source, source_len, "weather/source.url", &entries[count++]) != 0 ||
        copy_memory_to_zip(zip, frames_text, strlen(frames_text), "weather/frames.conf", &entries[count++]) != 0 ||
        add_fonts_to_backup(zip, entries, &count, &total_bytes) != 0 ||
        finish_zip(zip, entries, count) != 0;
    if (fclose(zip) != 0) failed = 1;
    free(entries);
    ipc_result_free(&config);
    if (failed) { unlink(output_path); return -1; }
    return 0;
}

static enum MHD_Result queue_backup_file(struct MHD_Connection *connection, const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return MHD_NO;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uint64_t)st.st_size > MAX_BACKUP_BYTES) { close(fd); return MHD_NO; }
    struct MHD_Response *response = MHD_create_response_from_fd64((uint64_t)st.st_size, fd);
    if (!response) { close(fd); return MHD_NO; }
    (void)add_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/zip");
    (void)add_header(response, "Content-Disposition", "attachment; filename=\"mk-clock-adult-backup.zip\"");
    add_api_headers(connection, response);
    enum MHD_Result result = MHD_queue_response(connection, 200, response);
    MHD_destroy_response(response);
    return result;
}

static enum MHD_Result download_backup(struct MHD_Connection *connection) {
    if (pthread_mutex_trylock(&g_maintenance_lock) != 0)
        return queue_json(connection, 409, "{\"ok\":false,\"error\":\"another backup or restore is running\"}");
    char path[] = "/tmp/mk-clock-adult-backup-XXXXXX";
    int fd = mkstemp(path);
    enum MHD_Result result;
    if (fd < 0) {
        result = queue_json(connection, 500, "{\"ok\":false,\"error\":\"backup could not be prepared\"}");
    } else {
        close(fd);
        if (build_adult_backup(path) != 0)
            result = queue_json(connection, 500, "{\"ok\":false,\"error\":\"backup could not be created\"}");
        else {
            result = queue_backup_file(connection, path);
            if (result == MHD_NO)
                result = queue_json(connection, 500, "{\"ok\":false,\"error\":\"backup could not be sent\"}");
        }
        unlink(path);
    }
    pthread_mutex_unlock(&g_maintenance_lock);
    return result;
}

static uint16_t read_le16(const unsigned char *value) {
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t read_le32(const unsigned char *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int restore_archive_path(const char *name, const char *stage_root,
                                char *output, size_t output_len) {
    const char *relative = NULL;
    if (strcmp(name, "config/clock.conf") == 0 || strcmp(name, "weather/source.url") == 0 ||
        strcmp(name, "weather/frames.conf") == 0) relative = name;
    else if (strncmp(name, "assets/fonts/", 13) == 0 && backup_font_allowed(name + 13)) relative = name;
    else if (strcmp(name, "backup.json") == 0) return 1;
    else return -1;
    if (strstr(relative, "..") || strchr(relative, '\\')) return -1;
    int written = snprintf(output, output_len, "%s/%s", stage_root, relative);
    return written > 0 && (size_t)written < output_len ? 0 : -1;
}

static int prepare_restore_stage(const char *stage_root) {
    static const char *const paths[] = {"config", "weather", "assets", "assets/fonts"};
    char path[PATH_MAX];
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        int written = snprintf(path, sizeof(path), "%s/%s", stage_root, paths[i]);
        if (written <= 0 || (size_t)written >= sizeof(path) || mp_asset_ensure_dir(path) != 0) return -1;
    }
    return 0;
}

static int extract_backup_archive(const char *archive_path, const char *stage_root, int *file_count) {
    if (prepare_restore_stage(stage_root) != 0) return -1;
    FILE *archive = fopen(archive_path, "rb");
    if (!archive) return -1;
    int count = 0, entry_count = 0;
    int saw_metadata = 0, saw_config = 0, saw_source = 0, saw_frames = 0;
    uint64_t total = 0;
    for (;;) {
        unsigned char signature_bytes[4];
        size_t got = fread(signature_bytes, 1, sizeof(signature_bytes), archive);
        if (got == 0 && feof(archive)) break;
        if (got != sizeof(signature_bytes)) { fclose(archive); return -1; }
        uint32_t signature = read_le32(signature_bytes);
        if (signature == 0x02014b50u || signature == 0x06054b50u) break;
        if (signature != 0x04034b50u || entry_count++ >= BACKUP_ENTRY_MAX) { fclose(archive); return -1; }
        unsigned char header[26];
        if (fread(header, 1, sizeof(header), archive) != sizeof(header)) { fclose(archive); return -1; }
        uint16_t flags = read_le16(header + 2), method = read_le16(header + 4);
        uint32_t expected_crc = read_le32(header + 10);
        uint32_t compressed_size = read_le32(header + 14), uncompressed_size = read_le32(header + 18);
        uint16_t name_length = read_le16(header + 22), extra_length = read_le16(header + 24);
        if (flags != 0 || method != 0 || compressed_size != uncompressed_size ||
            name_length == 0 || name_length >= 512 || total + uncompressed_size > MAX_BACKUP_BYTES) {
            fclose(archive); return -1;
        }
        char name[512];
        if (fread(name, 1, name_length, archive) != name_length) { fclose(archive); return -1; }
        name[name_length] = '\0';
        if (extra_length && fseeko(archive, extra_length, SEEK_CUR) != 0) { fclose(archive); return -1; }
        char output[PATH_MAX];
        int path_result = restore_archive_path(name, stage_root, output, sizeof(output));
        if (path_result == 1) {
            if (saw_metadata || uncompressed_size == 0 || uncompressed_size >= 4096) { fclose(archive); return -1; }
            char metadata[4096];
            if (fread(metadata, 1, uncompressed_size, archive) != uncompressed_size) { fclose(archive); return -1; }
            metadata[uncompressed_size] = '\0';
            if (crc32_update(0, (const unsigned char *)metadata, uncompressed_size) != expected_crc ||
                !strstr(metadata, "\"product\": \"mk-clock-adult\"") || !strstr(metadata, "\"format\": 1")) {
                fclose(archive); return -1;
            }
            saw_metadata = 1; total += uncompressed_size; continue;
        }
        if (path_result != 0) { fclose(archive); return -1; }
        if (strcmp(name, "config/clock.conf") == 0) { if (saw_config) { fclose(archive); return -1; } saw_config = 1; }
        if (strcmp(name, "weather/source.url") == 0) { if (saw_source) { fclose(archive); return -1; } saw_source = 1; }
        if (strcmp(name, "weather/frames.conf") == 0) { if (saw_frames) { fclose(archive); return -1; } saw_frames = 1; }
        int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0640);
        if (fd < 0) { fclose(archive); return -1; }
        unsigned char buffer[32768];
        uint32_t remaining = uncompressed_size, crc = 0;
        int failed = 0;
        while (remaining > 0) {
            size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            if (fread(buffer, 1, chunk, archive) != chunk || mp_write_full(fd, buffer, chunk) != 0) { failed = 1; break; }
            crc = crc32_update(crc, buffer, chunk);
            remaining -= (uint32_t)chunk;
        }
        if (fsync(fd) != 0 || close(fd) != 0) failed = 1;
        if (failed || crc != expected_crc) { fclose(archive); return -1; }
        total += uncompressed_size; count++;
    }
    fclose(archive);
    if (!saw_metadata || !saw_config || !saw_source || !saw_frames) return -1;
    if (file_count) *file_count = count;
    return 0;
}

static int remove_tree_contents_fd(int directory_fd, unsigned int depth) {
    if (directory_fd < 0 || depth > 64) { if (directory_fd >= 0) close(directory_fd); return -1; }
    DIR *directory = fdopendir(directory_fd);
    if (!directory) { close(directory_fd); return -1; }
    int failed = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        struct stat state;
        if (fstatat(dirfd(directory), entry->d_name, &state, AT_SYMLINK_NOFOLLOW) != 0) { failed = 1; continue; }
        if (S_ISDIR(state.st_mode)) {
            int child_fd = openat(dirfd(directory), entry->d_name, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
            if (child_fd < 0 || remove_tree_contents_fd(child_fd, depth + 1) != 0 ||
                unlinkat(dirfd(directory), entry->d_name, AT_REMOVEDIR) != 0) failed = 1;
        } else if (unlinkat(dirfd(directory), entry->d_name, 0) != 0) failed = 1;
    }
    if (closedir(directory) != 0) failed = 1;
    return failed ? -1 : 0;
}

static void remove_tree(const char *path) {
    if (!path || !*path) return;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) { (void)unlink(path); return; }
    (void)remove_tree_contents_fd(fd, 0);
    (void)rmdir(path);
}

static int clear_font_directory(void) {
    int fd = open(MP_FONT_DIR, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); return -1; }
    int failed = 0;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL) {
        if (item->d_name[0] == '.') continue;
        struct stat state;
        if (fstatat(dirfd(dir), item->d_name, &state, AT_SYMLINK_NOFOLLOW) != 0) { failed = 1; continue; }
        if (S_ISREG(state.st_mode) && unlinkat(dirfd(dir), item->d_name, 0) != 0) failed = 1;
    }
    if (closedir(dir) != 0) failed = 1;
    return failed ? -1 : 0;
}

static int copy_file_secure(const char *source, const char *target) {
    int in = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (in < 0) return -1;
    int out = open(target, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0640);
    if (out < 0) { close(in); return -1; }
    unsigned char buffer[32768];
    ssize_t got;
    int failed = 0;
    while ((got = read(in, buffer, sizeof(buffer))) > 0)
        if (mp_write_full(out, buffer, (size_t)got) != 0) { failed = 1; break; }
    if (got < 0 || fsync(out) != 0) failed = 1;
    close(in);
    if (close(out) != 0) failed = 1;
    if (failed) unlink(target);
    return failed ? -1 : 0;
}

static int copy_staged_fonts(const char *stage_root) {
    char source_dir[PATH_MAX];
    if (snprintf(source_dir, sizeof(source_dir), "%s/assets/fonts", stage_root) >= (int)sizeof(source_dir) ||
        mp_asset_ensure_dir(MP_FONT_DIR) != 0) return -1;
    DIR *dir = opendir(source_dir);
    if (!dir) return errno == ENOENT ? 0 : -1;
    int failed = 0;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL) {
        if (!backup_font_allowed(item->d_name)) continue;
        char source[PATH_MAX], target[PATH_MAX];
        if (snprintf(source, sizeof(source), "%s/%s", source_dir, item->d_name) >= (int)sizeof(source) ||
            snprintf(target, sizeof(target), "%s/%s", MP_FONT_DIR, item->d_name) >= (int)sizeof(target) ||
            copy_file_secure(source, target) != 0) { failed = 1; break; }
    }
    closedir(dir);
    return failed ? -1 : 0;
}

static int move_fonts(const char *source_dir, const char *target_dir) {
    if (mp_asset_ensure_dir(target_dir) != 0) return -1;
    DIR *dir = opendir(source_dir);
    if (!dir) return errno == ENOENT ? 0 : -1;
    int failed = 0;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL) {
        if (!backup_font_allowed(item->d_name)) continue;
        char source[PATH_MAX], target[PATH_MAX];
        if (snprintf(source, sizeof(source), "%s/%s", source_dir, item->d_name) >= (int)sizeof(source) ||
            snprintf(target, sizeof(target), "%s/%s", target_dir, item->d_name) >= (int)sizeof(target) ||
            rename(source, target) != 0) { failed = 1; break; }
    }
    closedir(dir);
    return failed ? -1 : 0;
}

static int stage_current_fonts(char *rollback_root, size_t rollback_root_len) {
    int written = snprintf(rollback_root, rollback_root_len, "%s/.restore-old-XXXXXX", MP_APP_ROOT "/assets");
    if (written <= 0 || (size_t)written >= rollback_root_len || !mkdtemp(rollback_root)) return -1;
    char target[PATH_MAX];
    if (snprintf(target, sizeof(target), "%s/fonts", rollback_root) >= (int)sizeof(target) ||
        move_fonts(MP_FONT_DIR, target) != 0) {
        remove_tree(rollback_root); rollback_root[0] = '\0'; return -1;
    }
    return 0;
}

static void restore_old_fonts(const char *rollback_root) {
    if (!rollback_root || !*rollback_root) return;
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/fonts", rollback_root);
    (void)clear_font_directory();
    (void)move_fonts(source, MP_FONT_DIR);
    remove_tree(rollback_root);
}

static int load_config_blob(const char *path, struct mp_ipc_config_blob *blob) {
    memset(blob, 0, sizeof(*blob));
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > MP_IPC_CONFIG_MAX_BYTES) {
        close(fd); return -1;
    }
    int ok = read_fd_full(fd, blob->data, (size_t)st.st_size) == 0;
    close(fd);
    if (!ok) return -1;
    blob->length = (uint32_t)st.st_size;
    return 0;
}

static int read_small_text_file(const char *path, char *output, size_t output_size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || (size_t)st.st_size >= output_size) {
        close(fd); return -1;
    }
    int ok = read_fd_full(fd, output, (size_t)st.st_size) == 0;
    close(fd);
    if (!ok) return -1;
    output[st.st_size] = '\0';
    output[strcspn(output, "\r\n")] = '\0';
    return output[0] ? 0 : -1;
}

static int core_alarm_active(void) {
    struct ipc_result status;
    memset(&status, 0, sizeof(status));
    if (ipc_call(MP_IPC_OP_STATUS, NULL, 0, &status) != 0) return 0;
    int active = status.body && status.body_len < MP_IPC_MAX_PAYLOAD &&
                 memmem(status.body, status.body_len, "\"alarm_active\":1", strlen("\"alarm_active\":1")) != NULL;
    ipc_result_free(&status);
    return active;
}

static enum MHD_Result restore_backup_locked(struct MHD_Connection *connection,
                                             struct request_context *context) {
    if (core_alarm_active())
        return queue_json(connection, 409, "{\"ok\":false,\"error\":\"dismiss the active alarm before restoring a backup\"}");
    if (context->upload_count != 1 || !context->uploads[0].temp_path[0])
        return queue_json(connection, 400, "{\"ok\":false,\"error\":\"choose one mk-clock-adult backup ZIP\"}");

    char stage_template[] = "/tmp/mk-clock-adult-restore-XXXXXX";
    char *stage = mkdtemp(stage_template);
    if (!stage) return queue_json(connection, 500, "{\"ok\":false,\"error\":\"restore staging could not be created\"}");
    int extracted = 0;
    if (extract_backup_archive(context->uploads[0].temp_path, stage, &extracted) != 0) {
        remove_tree(stage);
        return queue_json(connection, 400, "{\"ok\":false,\"error\":\"backup ZIP is invalid or unsupported\"}");
    }

    char path[PATH_MAX], error[192];
    struct mp_ipc_config_blob *new_config = calloc(1, sizeof(*new_config));
    if (!new_config) { remove_tree(stage); return queue_json(connection, 500, "{\"ok\":false,\"error\":\"allocation failed\"}"); }
    snprintf(path, sizeof(path), "%s/config/clock.conf", stage);
    if (load_config_blob(path, new_config) != 0) {
        free(new_config); remove_tree(stage);
        return queue_json(connection, 400, "{\"ok\":false,\"error\":\"backup configuration is missing\"}");
    }
    char new_source_raw[MP_WEATHER_SOURCE_URL_MAX], new_source[MP_WEATHER_SOURCE_URL_MAX];
    snprintf(path, sizeof(path), "%s/weather/source.url", stage);
    if (read_small_text_file(path, new_source_raw, sizeof(new_source_raw)) != 0 ||
        mp_weather_source_validate(new_source_raw, new_source, sizeof(new_source), error, sizeof(error)) != 0) {
        free(new_config); remove_tree(stage);
        return queue_json(connection, 400, "{\"ok\":false,\"error\":\"backup Weather source is invalid\"}");
    }
    struct mp_weather_frames_config new_frames;
    snprintf(path, sizeof(path), "%s/weather/frames.conf", stage);
    if (mp_weather_frames_read_path(path, &new_frames, error, sizeof(error)) != 0) {
        free(new_config); remove_tree(stage);
        return queue_json(connection, 400, "{\"ok\":false,\"error\":\"backup Weather panels are invalid\"}");
    }

    struct ipc_result old_config;
    memset(&old_config, 0, sizeof(old_config));
    if (ipc_call(MP_IPC_OP_CONFIG_EXPORT, NULL, 0, &old_config) != 0 || old_config.status != 200 ||
        old_config.content_type != MP_IPC_CONTENT_TEXT || old_config.body_len == 0 ||
        old_config.body_len > MP_IPC_CONFIG_MAX_BYTES) {
        ipc_result_free(&old_config); free(new_config); remove_tree(stage);
        return queue_json(connection, 503, "{\"ok\":false,\"error\":\"current settings could not be protected\"}");
    }
    struct mp_ipc_config_blob *rollback_config = calloc(1, sizeof(*rollback_config));
    if (!rollback_config) {
        ipc_result_free(&old_config); free(new_config); remove_tree(stage);
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"allocation failed\"}");
    }
    rollback_config->length = (uint32_t)old_config.body_len;
    memcpy(rollback_config->data, old_config.body, old_config.body_len);
    ipc_result_free(&old_config);

    char old_source[MP_WEATHER_SOURCE_URL_MAX];
    struct mp_weather_frames_config old_frames;
    if (mp_weather_source_read(old_source, sizeof(old_source), error, sizeof(error)) != 0 ||
        mp_weather_frames_read(&old_frames, error, sizeof(error)) != 0) {
        free(rollback_config); free(new_config); remove_tree(stage);
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"current Weather settings could not be protected\"}");
    }

    char rollback_root[PATH_MAX] = "";
    if (stage_current_fonts(rollback_root, sizeof(rollback_root)) != 0) {
        free(rollback_config); free(new_config); remove_tree(stage);
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"current fonts could not be protected\"}");
    }

    char canonical[MP_WEATHER_SOURCE_URL_MAX];
    int files_ok = copy_staged_fonts(stage) == 0;
    int weather_ok = files_ok &&
        mp_weather_source_write(new_source, canonical, sizeof(canonical), error, sizeof(error)) == 0 &&
        mp_weather_frames_write(&new_frames, error, sizeof(error)) == 0;
    struct ipc_result imported;
    memset(&imported, 0, sizeof(imported));
    int config_ok = weather_ok && ipc_call(MP_IPC_OP_CONFIG_IMPORT, new_config, sizeof(*new_config), &imported) == 0 &&
                    imported.status >= 200 && imported.status < 300;
    ipc_result_free(&imported);
    free(new_config);
    remove_tree(stage);

    if (!config_ok) {
        restore_old_fonts(rollback_root);
        (void)mp_weather_source_write(old_source, canonical, sizeof(canonical), error, sizeof(error));
        (void)mp_weather_frames_write(&old_frames, error, sizeof(error));
        struct ipc_result rolled_back;
        memset(&rolled_back, 0, sizeof(rolled_back));
        (void)ipc_call(MP_IPC_OP_CONFIG_IMPORT, rollback_config, sizeof(*rollback_config), &rolled_back);
        ipc_result_free(&rolled_back);
        free(rollback_config);
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"restore failed; previous settings and fonts were restored\"}");
    }

    free(rollback_config);
    remove_tree(rollback_root);
    char response[192];
    snprintf(response, sizeof(response), "{\"ok\":true,\"restored_files\":%d,\"music_preserved\":true}", extracted);
    return queue_json(connection, 200, response);
}

static enum MHD_Result restore_backup(struct MHD_Connection *connection,
                                      struct request_context *context) {
    if (pthread_mutex_trylock(&g_maintenance_lock) != 0)
        return queue_json(connection, 409, "{\"ok\":false,\"error\":\"another backup or restore is running\"}");
    enum MHD_Result result = restore_backup_locked(connection, context);
    pthread_mutex_unlock(&g_maintenance_lock);
    return result;
}


static struct form_field *field_slot(struct request_context *context, const char *key, uint64_t off) {
    for (size_t i = 0; i < context->field_count; i++) {
        if (strcmp(context->fields[i].key, key) == 0) {
            if (off == 0) {
                context->fields[i].len = 0;
                context->fields[i].value[0] = '\0';
            }
            return &context->fields[i];
        }
    }
    if (context->field_count >= FORM_FIELDS_MAX || off != 0) return NULL;
    if (context->field_count == context->field_cap) {
        size_t next = context->field_cap ? context->field_cap * 2 : 8;
        if (next > FORM_FIELDS_MAX) next = FORM_FIELDS_MAX;
        struct form_field *grown = realloc(context->fields, next * sizeof(*grown));
        if (!grown) return NULL;
        context->fields = grown;
        context->field_cap = next;
    }
    struct form_field *field = &context->fields[context->field_count++];
    memset(field, 0, sizeof(*field));
    mp_safe_str(field->key, sizeof(field->key), key);
    return field;
}

static struct upload_file *upload_slot(struct request_context *context, const char *key,
                                       const char *filename, uint64_t off) {
    if (off > 0) {
        for (size_t i = context->upload_count; i > 0; i--) {
            struct upload_file *upload = &context->uploads[i - 1];
            if (strcmp(upload->key, key) == 0 && strcmp(upload->filename, filename) == 0 &&
                upload->next_offset == off) return upload;
        }
        return NULL;
    }
    if (context->upload_count >= UPLOAD_FILES_MAX) return NULL;
    if (context->upload_count == context->upload_cap) {
        size_t next = context->upload_cap ? context->upload_cap * 2 : 4;
        if (next > UPLOAD_FILES_MAX) next = UPLOAD_FILES_MAX;
        struct upload_file *grown = realloc(context->uploads, next * sizeof(*grown));
        if (!grown) return NULL;
        context->uploads = grown;
        context->upload_cap = next;
    }
    struct upload_file *upload = &context->uploads[context->upload_count++];
    memset(upload, 0, sizeof(*upload));
    upload->fd = -1;
    mp_safe_str(upload->key, sizeof(upload->key), key);
    mp_safe_str(upload->filename, sizeof(upload->filename), filename);
    mp_safe_str(upload->temp_path, sizeof(upload->temp_path), "/tmp/mk-piclock-upload-XXXXXX");
    upload->fd = mkstemp(upload->temp_path);
    if (upload->fd < 0) {
        upload->temp_path[0] = '\0';
        return NULL;
    }
    return upload;
}

static int pwrite_full_at(int fd, const void *data, size_t size, uint64_t offset) {
    const unsigned char *p = data;
    size_t left = size;
    while (left > 0) {
        ssize_t n = pwrite(fd, p, left, (off_t)offset);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        p += (size_t)n;
        left -= (size_t)n;
        offset += (uint64_t)n;
    }
    return 0;
}

static enum MHD_Result post_iterator(void *cls, enum MHD_ValueKind kind, const char *key,
                                     const char *filename, const char *content_type,
                                     const char *transfer_encoding, const char *data,
                                     uint64_t off, size_t size) {
    (void)kind;
    (void)content_type;
    (void)transfer_encoding;
    struct request_context *context = cls;
    if (!key || context->parse_failed) return MHD_NO;
    if (context->body_too_large) return MHD_YES;
    if (filename && *filename) {
        if (context->upload_limit &&
            (off > context->upload_limit || size > context->upload_limit - (size_t)off)) {
            context->body_too_large = 1;
            return MHD_YES;
        }
        struct upload_file *upload = upload_slot(context, key, filename, off);
        if (!upload || upload->fd < 0 || upload->next_offset != off ||
            (size && pwrite_full_at(upload->fd, data, size, off) != 0)) {
            context->parse_failed = 1;
            return MHD_NO;
        }
        upload->next_offset += size;
        upload->size += size;
        return MHD_YES;
    }
    struct form_field *field = field_slot(context, key, off);
    if (!field || off != field->len || size > sizeof(field->value) - 1 - field->len) {
        context->parse_failed = 1;
        return MHD_NO;
    }
    memcpy(field->value + field->len, data, size);
    field->len += size;
    field->value[field->len] = '\0';
    return MHD_YES;
}

static const char *form_value(const struct request_context *context, const char *key) {
    for (size_t i = 0; i < context->field_count; i++)
        if (strcmp(context->fields[i].key, key) == 0) return context->fields[i].value;
    return NULL;
}

static int parse_int_value(const char *value, int fallback) {
    if (!value || !*value) return fallback;
    char *end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno || end == value || *end || parsed < -2147483647L || parsed > 2147483647L) return fallback;
    return (int)parsed;
}

static int form_int(const struct request_context *context, const char *key, int fallback) {
    return parse_int_value(form_value(context, key), fallback);
}

static uint64_t form_u64(const struct request_context *context, const char *key, uint64_t fallback) {
    const char *value = form_value(context, key);
    if (!value || !*value) return fallback;
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (errno || end == value || *end) return fallback;
    return (uint64_t)parsed;
}

static int parse_weather_slot_kind(const char *value, int fallback) {
    if (!value || !*value) return fallback;
    if (strcasecmp(value, "room") == 0) return MP_WEATHER_SLOT_ROOM;
    if (strcasecmp(value, "outside") == 0 || strcasecmp(value, "current") == 0)
        return MP_WEATHER_SLOT_OUTSIDE;
    if (strcasecmp(value, "forecast") == 0 || strcasecmp(value, "offset") == 0)
        return MP_WEATHER_SLOT_FORECAST;
    if (strcasecmp(value, "today") == 0 || strcasecmp(value, "daily") == 0)
        return MP_WEATHER_SLOT_TODAY;
    return 0;
}

static int parse_weather_icon(const char *value) {
    if (!value || !*value) return MP_WEATHER_ICON_UNKNOWN;
    if (strcasecmp(value, "clear") == 0 || strcasecmp(value, "sunny") == 0) return MP_WEATHER_ICON_CLEAR;
    if (strcasecmp(value, "partly") == 0 || strcasecmp(value, "partly-cloudy") == 0) return MP_WEATHER_ICON_PARTLY_CLOUDY;
    if (strcasecmp(value, "cloudy") == 0 || strcasecmp(value, "cloud") == 0) return MP_WEATHER_ICON_CLOUDY;
    if (strcasecmp(value, "rain") == 0 || strcasecmp(value, "rainy") == 0) return MP_WEATHER_ICON_RAIN;
    if (strcasecmp(value, "storm") == 0 || strcasecmp(value, "lightning") == 0 || strcasecmp(value, "thunderstorm") == 0) return MP_WEATHER_ICON_STORM;
    if (strcasecmp(value, "snow") == 0 || strcasecmp(value, "snowy") == 0) return MP_WEATHER_ICON_SNOW;
    if (strcasecmp(value, "wind") == 0 || strcasecmp(value, "windy") == 0) return MP_WEATHER_ICON_WIND;
    if (strcasecmp(value, "fog") == 0 || strcasecmp(value, "foggy") == 0) return MP_WEATHER_ICON_FOG;
    return MP_WEATHER_ICON_UNKNOWN;
}

static int parse_time_value(const char *value, int *hour, int *minute) {
    int h = 0, m = 0;
    char extra = '\0';
    if (!value || sscanf(value, "%d:%d%c", &h, &m, &extra) != 2 || h < 0 || h > 23 || m < 0 || m > 59)
        return -1;
    *hour = h;
    *minute = m;
    return 0;
}

static void close_uploads(struct request_context *context) {
    for (size_t i = 0; i < context->upload_count; i++) {
        if (context->uploads[i].fd >= 0) {
            if (fsync(context->uploads[i].fd) != 0) context->parse_failed = 1;
            if (close(context->uploads[i].fd) != 0) context->parse_failed = 1;
            context->uploads[i].fd = -1;
        }
    }
}

static void cleanup_context(struct request_context *context) {
    if (!context) return;
    if (context->post) MHD_destroy_post_processor(context->post);
    close_uploads(context);
    for (size_t i = 0; i < context->upload_count; i++) {
        if (context->uploads[i].temp_path[0]) unlink(context->uploads[i].temp_path);
    }
    free(context->fields);
    free(context->uploads);
    free(context);
}

static const char *query_value(struct MHD_Connection *connection, const char *key) {
    return MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, key);
}

static void system_font_display_name(FT_Library library,
                                     const struct mp_system_font_entry *entry,
                                     char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (library && entry) {
        FT_Face face = NULL;
        if (FT_New_Face(library, entry->path, 0, &face) == 0) {
            const char *family = face->family_name ? face->family_name : "";
            const char *style = face->style_name ? face->style_name : "";
            if (family[0] && style[0] && strcasecmp(style, "Regular") != 0 &&
                strcasecmp(style, "Book") != 0)
                snprintf(out, out_len, "%s %s", family, style);
            else if (family[0])
                mp_safe_str(out, out_len, family);
            FT_Done_Face(face);
        }
    }
    if (!out[0] && entry) mp_safe_str(out, out_len, entry->filename);
}

static enum MHD_Result serve_fonts_list(struct MHD_Connection *connection) {
    struct mp_ipc_asset_state state;
    if (get_asset_state(&state) != 0)
        return queue_json(connection, 503, "{\"ok\":false,\"error\":\"clock core unavailable\"}");

    char files[MP_ASSET_LIST_MAX][MP_ASSET_NAME_MAX];
    int uploaded_count = mp_asset_scan(MP_FONT_DIR, MP_ASSET_SCAN_FONT, files, MP_ASSET_LIST_MAX);
    struct mp_system_font_entry *system_fonts = calloc(MP_SYSTEM_FONT_LIST_MAX, sizeof(*system_fonts));
    int system_count = system_fonts ? mp_system_font_scan(system_fonts, MP_SYSTEM_FONT_LIST_MAX) : 0;
    if (system_fonts && mp_system_font_is_key(state.selected_font)) {
        int selected_present = 0;
        for (int i = 0; i < system_count; i++) {
            if (strcmp(system_fonts[i].key, state.selected_font) == 0) {
                selected_present = 1;
                break;
            }
        }
        if (!selected_present) {
            char selected_path[MP_SYSTEM_FONT_PATH_MAX];
            if (mp_system_font_resolve(state.selected_font, selected_path, sizeof(selected_path)) == 0) {
                int slot = system_count < MP_SYSTEM_FONT_LIST_MAX ? system_count++ : MP_SYSTEM_FONT_LIST_MAX - 1;
                mp_safe_str(system_fonts[slot].key, sizeof(system_fonts[slot].key), state.selected_font);
                mp_safe_str(system_fonts[slot].path, sizeof(system_fonts[slot].path), selected_path);
                const char *base = strrchr(selected_path, '/');
                mp_safe_str(system_fonts[slot].filename, sizeof(system_fonts[slot].filename), base ? base + 1 : selected_path);
            }
        }
    }
    FT_Library library = NULL;
    (void)FT_Init_FreeType(&library);

    char default_system_key[MP_SYSTEM_FONT_KEY_MAX] = "";
    for (int i = 0; i < system_count; i++) {
        if (strcasecmp(system_fonts[i].filename, "DejaVuSansMono.ttf") == 0) {
            mp_safe_str(default_system_key, sizeof(default_system_key), system_fonts[i].key);
            break;
        }
    }

    struct mp_buffer body;
    if (mp_buffer_init(&body, 8192, MAX_API_JSON_RESPONSE) != 0) {
        if (library) FT_Done_FreeType(library);
        free(system_fonts);
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"allocation failed\"}");
    }
    mp_buffer_append(&body, "{\"selected\":\"");
    mp_buffer_append_json_string(&body, state.selected_font);
    mp_buffer_appendf(&body,
        "\",\"builtin\":%d,\"font_size\":%d,\"default_system_key\":\"",
        state.builtin_font, state.font_size);
    mp_buffer_append_json_string(&body, default_system_key);
    mp_buffer_append(&body,
        "\",\"builtin_fonts\":["
        "{\"id\":0,\"name\":\"Seven Segment\"},{\"id\":1,\"name\":\"Seven Thin\"},"
        "{\"id\":2,\"name\":\"Pixel\"},{\"id\":3,\"name\":\"Pixel Bold\"},"
        "{\"id\":4,\"name\":\"Automatic font detection\"}],"
        "\"system_fonts\":[");
    for (int i = 0; i < system_count && !body.failed; i++) {
        char display_name[256];
        system_font_display_name(library, &system_fonts[i], display_name, sizeof(display_name));
        mp_buffer_appendf(&body, "%s{\"key\":\"", i ? "," : "");
        mp_buffer_append_json_string(&body, system_fonts[i].key);
        mp_buffer_append(&body, "\",\"name\":\"");
        mp_buffer_append_json_string(&body, display_name);
        mp_buffer_append(&body, "\",\"file\":\"");
        mp_buffer_append_json_string(&body, system_fonts[i].filename);
        mp_buffer_append(&body, "\"}");
    }
    mp_buffer_append(&body, "],\"uploaded_fonts\":[");
    for (int i = 0; i < uploaded_count && !body.failed; i++) {
        mp_buffer_appendf(&body, "%s\"", i ? "," : "");
        mp_buffer_append_json_string(&body, files[i]);
        mp_buffer_append(&body, "\"");
    }
    mp_buffer_append(&body, "]}");

    if (library) FT_Done_FreeType(library);
    free(system_fonts);
    return queue_json_builder(connection, 200, &body);
}

static enum MHD_Result serve_music_list(struct MHD_Connection *connection) {
    struct mp_ipc_asset_state state;
    if (get_asset_state(&state) != 0)
        return queue_json(connection, 503, "{\"ok\":false,\"error\":\"clock core unavailable\"}");
    char files[MP_ASSET_LIST_MAX][MP_ASSET_NAME_MAX];
    int count = mp_asset_scan(MP_MUSIC_DIR, MP_ASSET_SCAN_MUSIC_MP3, files, MP_ASSET_LIST_MAX);
    struct mp_buffer body;
    if (mp_buffer_init(&body, 4096, MAX_API_JSON_RESPONSE) != 0)
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"allocation failed\"}");
    mp_buffer_appendf(&body, "{\"global_volume\":%d,\"current\":\"", state.global_volume);
    mp_buffer_append_json_string(&body, state.current_music);
    mp_buffer_append(&body, "\",\"tracks\":[");
    for (int i = 0; i < count && !body.failed; i++) {
        char path[768];
        int path_len = snprintf(path, sizeof(path), "%s/%.*s", MP_MUSIC_DIR,
                                MP_ASSET_NAME_MAX - 1, files[i]);
        struct mp_id3_metadata metadata;
        memset(&metadata, 0, sizeof(metadata));
        int tagged = path_len >= 0 && (size_t)path_len < sizeof(path) &&
                     mp_read_id3_metadata(path, &metadata) == 0;
        struct mp_audio_info audio_info;
        memset(&audio_info, 0, sizeof(audio_info));
        if (path_len >= 0 && (size_t)path_len < sizeof(path))
            (void)mp_asset_read_mp3_info(path, &audio_info);
        if (!metadata.title[0]) mp_title_from_filename(files[i], metadata.title, sizeof(metadata.title));
        char display[MP_ID3_TEXT_MAX * 2 + 4];
        if (metadata.artist[0])
            snprintf(display, sizeof(display), "%s - %s", metadata.title, metadata.artist);
        else
            mp_safe_str(display, sizeof(display), metadata.title);

        mp_buffer_appendf(&body, "%s{\"file\":\"", i ? "," : "");
        mp_buffer_append_json_string(&body, files[i]);
        mp_buffer_append(&body, "\",\"title\":\"");
        mp_buffer_append_json_string(&body, metadata.title);
        mp_buffer_append(&body, "\",\"artist\":\"");
        mp_buffer_append_json_string(&body, metadata.artist);
        mp_buffer_append(&body, "\",\"album\":\"");
        mp_buffer_append_json_string(&body, metadata.album);
        mp_buffer_append(&body, "\",\"year\":\"");
        mp_buffer_append_json_string(&body, metadata.year);
        mp_buffer_append(&body, "\",\"track\":\"");
        mp_buffer_append_json_string(&body, metadata.track);
        mp_buffer_append(&body, "\",\"genre\":\"");
        mp_buffer_append_json_string(&body, metadata.genre);
        mp_buffer_append(&body, "\",\"display\":\"");
        mp_buffer_append_json_string(&body, display);
        mp_buffer_appendf(&body,
            "\",\"id3\":%d,\"duration_seconds\":%.3f,\"duration_estimated\":%d,"
            "\"bitrate_kbps\":%d,\"bitrate_mode\":\"%s\",\"sample_rate_hz\":%ld,"
            "\"channels\":%d,\"mpeg_layer\":%d,\"file_size_bytes\":%llu}",
            tagged ? 1 : 0, audio_info.duration_seconds, audio_info.duration_estimated,
            audio_info.bitrate_kbps, audio_info.vbr_mode == 1 ? "VBR" : audio_info.vbr_mode == 2 ? "ABR" : "CBR",
            audio_info.sample_rate_hz, audio_info.channels, audio_info.layer,
            (unsigned long long)audio_info.file_size_bytes);
    }
    mp_buffer_append(&body, "]}");
    return queue_json_builder(connection, 200, &body);
}

static enum MHD_Result serve_music_jobs(struct MHD_Connection *connection) {
    struct mp_music_job_snapshot jobs[MP_MUSIC_JOB_MAX];
    int count = mp_music_jobs_snapshot(jobs, MP_MUSIC_JOB_MAX);
    struct mp_buffer body;
    if (mp_buffer_init(&body, 2048, MAX_API_JSON_RESPONSE) != 0)
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"allocation failed\"}");
    mp_buffer_append(&body, "{\"ok\":true,\"jobs\":[");
    for (int i = 0; i < count && !body.failed; i++) {
        mp_buffer_appendf(&body, "%s{\"id\":%llu,\"state\":\"%s\",\"progress\":%u,\"file\":\"",
                          i ? "," : "", (unsigned long long)jobs[i].id,
                          mp_music_job_state_name(jobs[i].state), jobs[i].progress);
        mp_buffer_append_json_string(&body, jobs[i].file);
        mp_buffer_append(&body, "\",\"error\":\"");
        mp_buffer_append_json_string(&body, jobs[i].error);
        mp_buffer_appendf(&body,
            "\",\"created_at\":%lld,\"completed_at\":%lld,\"bitrate_kbps\":%d,"
            "\"sample_rate_hz\":%ld,\"lowpass_hz\":%d}",
            (long long)jobs[i].created_at, (long long)jobs[i].completed_at,
            jobs[i].settings.bitrate_kbps, jobs[i].settings.sample_rate_hz,
            jobs[i].settings.lowpass_hz);
    }
    mp_buffer_append(&body, "]}");
    return queue_json_builder(connection, 200, &body);
}

static enum MHD_Result serve_font_file(struct MHD_Connection *connection) {
    const char *key = query_value(connection, "key");
    char path[MP_SYSTEM_FONT_PATH_MAX];
    if (key && key[0]) {
        if (mp_system_font_resolve(key, path, sizeof(path)) != 0)
            return queue_json(connection, 404, "{\"ok\":false,\"error\":\"system font not found\"}");
    } else {
        const char *file = query_value(connection, "file");
        if (!mp_asset_safe_filename(file) || !mp_asset_has_font_ext(file))
            return queue_json(connection, 400, "{\"ok\":false,\"error\":\"invalid font file\"}");
        snprintf(path, sizeof(path), "%s/%s", MP_FONT_DIR, file);
    }
    enum MHD_Result result = queue_file(connection, path, content_type_for_path(path), 1, 0);
    return result == MHD_NO ? queue_json(connection, 404, "{\"ok\":false,\"error\":\"font not found\"}") : result;
}

static enum MHD_Result notify_or_saved_warning(struct MHD_Connection *connection, uint8_t kind,
                                                uint8_t action, uint32_t count, const char *file,
                                                const char *success_json) {
    if (notify_asset(kind, action, count, file) != 0)
        return queue_json(connection, 503,
            "{\"ok\":false,\"saved\":true,\"error\":\"asset saved but clock core could not reload it\"}");
    return queue_json(connection, 200, success_json);
}

static int valid_music_settings(const struct mp_audio_optimize_settings *settings) {
    if (!settings) return 0;
    int bitrate_ok = settings->bitrate_kbps == 64 || settings->bitrate_kbps == 96 ||
                     settings->bitrate_kbps == 128 || settings->bitrate_kbps == 160;
    int rate_ok = settings->sample_rate_hz == 32000 || settings->sample_rate_hz == 44100;
    int lowpass_ok = settings->lowpass_hz == 12000 || settings->lowpass_hz == 16000 ||
                     settings->lowpass_hz == 18000;
    return bitrate_ok && rate_ok && lowpass_ok;
}

static enum MHD_Result upload_music(struct MHD_Connection *connection, struct request_context *context) {
    if (mp_asset_ensure_dir(MP_MUSIC_DIR) != 0 || mp_asset_ensure_dir(MP_MUSIC_PROCESS_DIR) != 0)
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"music processing directory unavailable\"}");
    if (mp_music_jobs_active() > 0)
        return queue_json(connection, 409,
                          "{\"ok\":false,\"error\":\"wait for all selected songs to finish before uploading more\"}");
    if (context->upload_count == 0 || context->upload_count > MP_MUSIC_JOB_MAX)
        return queue_json(connection, 400,
                          "{\"ok\":false,\"error\":\"upload between 1 and 32 MP3 files at a time\"}");

    struct mp_audio_optimize_settings settings = {
        .bitrate_kbps = form_int(context, "bitrate_kbps", 96),
        .sample_rate_hz = form_int(context, "sample_rate_hz", 44100),
        .lowpass_hz = form_int(context, "lowpass_hz", 16000),
        .quality = 2
    };
    if (!valid_music_settings(&settings))
        return queue_json(connection, 400, "{\"ok\":false,\"error\":\"unsupported MP3 processing settings\"}");

    char names[MP_MUSIC_JOB_MAX][MP_ASSET_NAME_MAX];
    char staged[MP_MUSIC_JOB_MAX][768];
    struct mp_music_job_request requests[MP_MUSIC_JOB_MAX];
    uint64_t job_ids[MP_MUSIC_JOB_MAX];
    uint64_t required = 0;
    memset(staged, 0, sizeof(staged));
    memset(requests, 0, sizeof(requests));
    memset(job_ids, 0, sizeof(job_ids));

    for (size_t i = 0; i < context->upload_count; i++) {
        struct upload_file *upload = &context->uploads[i];
        mp_asset_sanitize_filename(upload->filename, names[i], sizeof(names[i]), "alarm.mp3");
        if (!mp_asset_safe_filename(names[i]) || !mp_asset_has_mp3_ext(names[i]) ||
            upload->size == 0 || upload->size > MP_MUSIC_UPLOAD_MAX_BYTES ||
            mp_asset_validate_mp3(upload->temp_path) != 0)
            return queue_json(connection, 400,
                              "{\"ok\":false,\"error\":\"every selected file must be a readable MP3\"}");
        for (size_t prior = 0; prior < i; prior++) {
            if (strcmp(names[prior], names[i]) == 0)
                return queue_json(connection, 400,
                                  "{\"ok\":false,\"error\":\"selected MP3 files must have unique filenames\"}");
        }
        if (upload->size > (UINT64_MAX - required) / 2u)
            return queue_json(connection, 400, "{\"ok\":false,\"error\":\"upload size overflow\"}");
        required += (uint64_t)upload->size * 2u;
    }

    if (!mp_asset_has_free_space(MP_MUSIC_DIR, required, g_disk_reserve_bytes))
        return queue_json(connection, 507, "{\"ok\":false,\"error\":\"insufficient storage\"}");

    size_t staged_count = 0;
    for (size_t i = 0; i < context->upload_count; i++) {
        snprintf(staged[i], sizeof(staged[i]), "%s/.source-XXXXXX", MP_MUSIC_PROCESS_DIR);
        int source_fd = mkstemp(staged[i]);
        if (source_fd < 0) break;
        close(source_fd);
        unlink(staged[i]);
        if (mp_asset_move_file(context->uploads[i].temp_path, staged[i]) != 0) break;
        context->uploads[i].temp_path[0] = '\0';
        requests[i].source_path = staged[i];
        requests[i].output_name = names[i];
        staged_count++;
    }
    if (staged_count != context->upload_count) {
        for (size_t i = 0; i < staged_count; i++) (void)unlink(staged[i]);
        return queue_json(connection, 500,
                          "{\"ok\":false,\"error\":\"one or more music files could not be staged\"}");
    }

    int queue_result = mp_music_jobs_queue_batch(requests, context->upload_count,
                                                  &settings, job_ids);
    if (queue_result != 0) {
        for (size_t i = 0; i < staged_count; i++) (void)unlink(staged[i]);
        if (queue_result == 1)
            return queue_json(connection, 409,
                              "{\"ok\":false,\"error\":\"wait for all selected songs to finish before uploading more\"}");
        if (queue_result == 2)
            return queue_json(connection, 409,
                              "{\"ok\":false,\"error\":\"music processing queue does not have enough free slots\"}");
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"music could not be queued\"}");
    }

    struct mp_buffer body;
    if (mp_buffer_init(&body, 512, 8192) != 0)
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"allocation failed\"}");
    mp_buffer_appendf(&body,
        "{\"ok\":true,\"queued\":%zu,\"skipped\":0,\"settings\":{\"bitrate_kbps\":%d,"
        "\"sample_rate_hz\":%ld,\"lowpass_hz\":%d},\"job_ids\":[",
        context->upload_count, settings.bitrate_kbps, settings.sample_rate_hz, settings.lowpass_hz);
    for (size_t i = 0; i < context->upload_count; i++)
        mp_buffer_appendf(&body, "%s%llu", i ? "," : "", (unsigned long long)job_ids[i]);
    mp_buffer_append(&body, "]}");
    return queue_json_builder(connection, 202, &body);
}

static enum MHD_Result upload_font(struct MHD_Connection *connection, struct request_context *context) {
    if (mp_asset_ensure_dir(MP_FONT_DIR) != 0)
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"font directory unavailable\"}");
    for (size_t i = 0; i < context->upload_count; i++) {
        struct upload_file *upload = &context->uploads[i];
        char name[MP_ASSET_NAME_MAX];
        mp_asset_sanitize_filename(upload->filename, name, sizeof(name), "uploaded_font.ttf");
        if (!mp_asset_safe_filename(name) || !mp_asset_has_font_ext(name) || upload->size == 0 ||
            upload->size > MP_FONT_UPLOAD_MAX_BYTES) continue;
        if (!mp_asset_has_free_space(MP_FONT_DIR, upload->size, g_disk_reserve_bytes))
            return queue_json(connection, 507, "{\"ok\":false,\"error\":\"insufficient storage\"}");
        if (mp_asset_validate_font(upload->temp_path) != 0)
            return queue_json(connection, 400, "{\"ok\":false,\"error\":\"uploaded file is not a readable font\"}");
        char target[768];
        snprintf(target, sizeof(target), "%s/%s", MP_FONT_DIR, name);
        if (mp_asset_move_file(upload->temp_path, target) != 0)
            return queue_json(connection, 500, "{\"ok\":false,\"error\":\"could not save font\"}");
        upload->temp_path[0] = '\0';
        return notify_or_saved_warning(connection, MP_IPC_ASSET_FONT, MP_IPC_ASSET_UPLOADED,
                                       1, name, "{\"ok\":true,\"uploaded\":1}");
    }
    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"no valid font was uploaded\"}");
}

static enum MHD_Result delete_music(struct MHD_Connection *connection,
                                    const struct request_context *context) {
    const char *file = form_value(context, "file");
    if (!mp_asset_safe_filename(file) || !mp_asset_has_mp3_ext(file))
        return queue_json(connection, 400, "{\"ok\":false,\"error\":\"invalid music filename\"}");
    if (mp_music_jobs_file_active(file))
        return queue_json(connection, 409, "{\"ok\":false,\"error\":\"this music file is still processing\"}");
    if (mp_asset_delete_file(MP_MUSIC_DIR, file) != 0)
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"music file could not be deleted\"}");
    if (notify_asset(MP_IPC_ASSET_MUSIC, MP_IPC_ASSET_DELETED, 1, file) != 0)
        return queue_json(connection, 503,
                          "{\"ok\":false,\"deleted\":true,\"error\":\"music deleted but clock core unavailable\"}");
    return queue_json(connection, 200, "{\"ok\":true,\"deleted\":true}");
}

static enum MHD_Result delete_all_music(struct MHD_Connection *connection) {
    if (mp_music_jobs_active() > 0)
        return queue_json(connection, 409,
                          "{\"ok\":false,\"error\":\"wait for music processing to finish before deleting all music\"}");
    int deleted = mp_asset_delete_music();
    if (notify_asset(MP_IPC_ASSET_MUSIC, MP_IPC_ASSET_DELETED_ALL, (uint32_t)deleted, NULL) != 0)
        return queue_json(connection, 503, "{\"ok\":false,\"deleted\":true,\"error\":\"clock core unavailable\"}");
    char json[128];
    snprintf(json, sizeof(json), "{\"ok\":true,\"deleted_music\":%d}", deleted);
    return queue_json(connection, 200, json);
}

static enum MHD_Result clear_music_queue(struct MHD_Connection *connection) {
    int removed = mp_music_jobs_clear_queued();
    int processing_active = mp_music_jobs_active() > 0;
    char json[160];
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"queued_removed\":%d,\"processing_active\":%s}",
             removed, processing_active ? "true" : "false");
    return queue_json(connection, 200, json);
}

static enum MHD_Result delete_font(struct MHD_Connection *connection, const struct request_context *context) {
    const char *file = form_value(context, "font");
    if (!mp_asset_safe_filename(file) || !mp_asset_has_font_ext(file))
        return queue_json(connection, 400, "{\"ok\":false,\"error\":\"invalid font filename\"}");
    (void)mp_asset_delete_file(MP_FONT_DIR, file);
    if (notify_asset(MP_IPC_ASSET_FONT, MP_IPC_ASSET_DELETED, 1, file) != 0)
        return queue_json(connection, 503, "{\"ok\":false,\"deleted\":true,\"error\":\"clock core unavailable\"}");
    return queue_json(connection, 200, "{\"ok\":true,\"deleted\":true}");
}


static void weather_source_json_escape(char *output, size_t output_size, const char *input) {
    if (!output || output_size == 0) return;
    if (!input) input = "";
    size_t used = 0;
    for (const unsigned char *cursor = (const unsigned char *)input; *cursor; cursor++) {
        const char *escape = NULL;
        char unicode[7];
        if (*cursor == '"') escape = "\\\"";
        else if (*cursor == '\\') escape = "\\\\";
        else if (*cursor == '\n') escape = "\\n";
        else if (*cursor == '\r') escape = "\\r";
        else if (*cursor == '\t') escape = "\\t";
        else if (*cursor < 0x20) {
            snprintf(unicode, sizeof(unicode), "\\u%04x", *cursor);
            escape = unicode;
        }

        if (escape) {
            size_t length = strlen(escape);
            if (used + length + 1 >= output_size) break;
            memcpy(output + used, escape, length);
            used += length;
        } else {
            if (used + 2 >= output_size) break;
            output[used++] = (char)*cursor;
        }
    }
    output[used] = '\0';
}

static enum MHD_Result weather_source_get(struct MHD_Connection *connection) {
    char url[MP_WEATHER_SOURCE_URL_MAX];
    char error[192];
    if (mp_weather_source_read(url, sizeof(url), error, sizeof(error)) != 0) {
        char escaped[384];
        char json[512];
        weather_source_json_escape(escaped, sizeof(escaped), error);
        snprintf(json, sizeof(json),
            "{\"ok\":false,\"error\":\"%s\"}", escaped);
        return queue_json(connection, 500, json);
    }

    char escaped_url[MP_WEATHER_SOURCE_URL_MAX * 2];
    char escaped_default[MP_WEATHER_SOURCE_URL_MAX * 2];
    char json[MP_WEATHER_SOURCE_URL_MAX * 4 + 128];
    weather_source_json_escape(escaped_url, sizeof(escaped_url), url);
    weather_source_json_escape(escaped_default, sizeof(escaped_default), MP_WEATHER_SOURCE_DEFAULT);
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"editable\":true,\"url\":\"%s\","
        "\"default_url\":\"%s\",\"source_file\":\"%s\"}",
        escaped_url, escaped_default, MP_WEATHER_SOURCE_FILE);
    return queue_json(connection, 200, json);
}

static enum MHD_Result weather_source_set(
    struct MHD_Connection *connection,
    const struct request_context *context
) {
    char previous[MP_WEATHER_SOURCE_URL_MAX];
    char canonical[MP_WEATHER_SOURCE_URL_MAX];
    char error[192];
    int had_previous = mp_weather_source_read(
        previous, sizeof(previous), error, sizeof(error)) == 0;
    if (mp_weather_source_write(
            form_value(context, "url"),
            canonical,
            sizeof(canonical),
            error,
            sizeof(error)) != 0) {
        char escaped[384];
        char json[512];
        weather_source_json_escape(escaped, sizeof(escaped), error);
        snprintf(json, sizeof(json),
            "{\"ok\":false,\"error\":\"%s\"}", escaped);
        return queue_json(connection, 400, json);
    }

    char escaped[MP_WEATHER_SOURCE_URL_MAX * 2];
    char json[MP_WEATHER_SOURCE_URL_MAX * 2 + 128];
    weather_source_json_escape(escaped, sizeof(escaped), canonical);
    int changed = !had_previous || strcmp(previous, canonical) != 0;
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"editable\":true,\"url\":\"%s\","
        "\"changed\":%s,\"refresh_requested\":true}",
        escaped, changed ? "true" : "false");
    return queue_json(connection, 200, json);
}



static enum MHD_Result weather_frames_get(struct MHD_Connection *connection) {
    struct mp_weather_frames_config config;
    char error[192];
    if (mp_weather_frames_read(&config, error, sizeof(error)) != 0) {
        char escaped[384];
        char json[512];
        weather_source_json_escape(escaped, sizeof(escaped), error);
        snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", escaped);
        return queue_json(connection, 500, json);
    }

    char json[896];
    snprintf(
        json,
        sizeof(json),
        "{\"ok\":true,\"editable\":true,\"config_file\":\"%s\","
        "\"slot1\":{\"mode\":\"%s\",\"offset_hours\":%d,\"time\":\"%02d:00\"},"
        "\"slot2\":{\"mode\":\"%s\",\"offset_hours\":%d,\"time\":\"%02d:00\"},"
        "\"slot3\":{\"mode\":\"%s\",\"offset_hours\":%d,\"time\":\"%02d:00\"}}",
        MP_WEATHER_FRAMES_FILE,
        mp_weather_frame_mode_name(config.slots[0].mode), config.slots[0].offset_hours,
        config.slots[0].time_hour,
        mp_weather_frame_mode_name(config.slots[1].mode), config.slots[1].offset_hours,
        config.slots[1].time_hour,
        mp_weather_frame_mode_name(config.slots[2].mode), config.slots[2].offset_hours,
        config.slots[2].time_hour
    );
    return queue_json(connection, 200, json);
}

static enum MHD_Result weather_frames_set(
    struct MHD_Connection *connection,
    const struct request_context *context
) {
    struct mp_weather_frames_config previous;
    struct mp_weather_frames_config config;
    char error[192];
    if (mp_weather_frames_read(&config, error, sizeof(error)) != 0)
        mp_weather_frames_defaults(&config);
    previous = config;

    for (int index = 0; index < MP_WEATHER_PANEL_COUNT; index++) {
        char key[48];
        snprintf(key, sizeof(key), "slot%d_mode", index + 1);
        const char *mode = form_value(context, key);
        if (mode && *mode &&
            mp_weather_frame_mode_parse(mode, &config.slots[index].mode) != 0) {
            return queue_json(
                connection,
                400,
                "{\"ok\":false,\"error\":\"weather panel mode must be room, outside, today, offset, or time\"}"
            );
        }

        snprintf(key, sizeof(key), "slot%d_offset_hours", index + 1);
        if (form_value(context, key))
            config.slots[index].offset_hours =
                form_int(context, key, config.slots[index].offset_hours);

        snprintf(key, sizeof(key), "slot%d_time", index + 1);
        const char *time_value = form_value(context, key);
        if (time_value) {
            int hour = 0;
            int minute = 0;
            if (parse_time_value(time_value, &hour, &minute) != 0 || minute != 0) {
                return queue_json(
                    connection,
                    400,
                    "{\"ok\":false,\"error\":\"specific weather-panel time must be on the hour\"}"
                );
            }
            config.slots[index].time_hour = hour;
        }
    }

    if (mp_weather_frames_write(&config, error, sizeof(error)) != 0) {
        char escaped[384];
        char json[512];
        weather_source_json_escape(escaped, sizeof(escaped), error);
        snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", escaped);
        return queue_json(connection, 400, json);
    }

    int changed = memcmp(&previous, &config, sizeof(config)) != 0;
    char json[896];
    snprintf(
        json,
        sizeof(json),
        "{\"ok\":true,\"changed\":%s,\"refresh_requested\":true,"
        "\"slot1\":{\"mode\":\"%s\",\"offset_hours\":%d,\"time\":\"%02d:00\"},"
        "\"slot2\":{\"mode\":\"%s\",\"offset_hours\":%d,\"time\":\"%02d:00\"},"
        "\"slot3\":{\"mode\":\"%s\",\"offset_hours\":%d,\"time\":\"%02d:00\"}}",
        changed ? "true" : "false",
        mp_weather_frame_mode_name(config.slots[0].mode), config.slots[0].offset_hours,
        config.slots[0].time_hour,
        mp_weather_frame_mode_name(config.slots[1].mode), config.slots[1].offset_hours,
        config.slots[1].time_hour,
        mp_weather_frame_mode_name(config.slots[2].mode), config.slots[2].offset_hours,
        config.slots[2].time_hour
    );
    return queue_json(connection, 200, json);
}

static enum MHD_Result weather_activity_get(struct MHD_Connection *connection) {
    char status[MP_WEATHER_STATUS_JSON_MAX];
    char activity[MP_WEATHER_ACTIVITY_JSON_MAX];
    char error[192];
    if (mp_weather_status_read_json(status, sizeof(status), error, sizeof(error)) != 0 ||
        mp_weather_activity_read_json(activity, sizeof(activity), error, sizeof(error)) != 0) {
        char escaped[384];
        char json[512];
        weather_source_json_escape(escaped, sizeof(escaped), error);
        snprintf(json, sizeof(json),
            "{\"ok\":false,\"error\":\"%s\"}", escaped);
        return queue_json(connection, 500, json);
    }

    size_t needed = strlen(status) + strlen(activity) + 64;
    char *json = malloc(needed);
    if (!json) {
        return queue_json(connection, 500,
            "{\"ok\":false,\"error\":\"Out of memory\"}");
    }
    snprintf(json, needed,
        "{\"ok\":true,\"status\":%s,\"activity\":%s}",
        status, activity);
    enum MHD_Result result = queue_json(connection, 200, json);
    free(json);
    return result;
}


static enum MHD_Result serve_network_diagnostics(struct MHD_Connection *connection) {
    struct adult_diagnostic_info info;
    collect_adult_diagnostics(&info);
    struct mp_buffer body;
    if (mp_buffer_init(&body, 4096, 32768) != 0)
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"allocation failed\"}");
    mp_buffer_append(&body, "{\"ok\":true");
    diag_append_json_string(&body, "product_version", PRODUCT_VERSION);
    diag_append_json_string(&body, "api_version", API_VERSION);
    diag_append_json_string(&body, "weather_version", MP_WEATHER_VERSION);
    diag_append_json_string(&body, "compiled_at", BUILD_TIMESTAMP);
    diag_append_json_string(&body, "hostname", info.network.hostname);
    diag_append_json_string(&body, "interface", info.network.interface_name);
    diag_append_json_string(&body, "ip_address", info.network.ip_address);
    diag_append_json_string(&body, "ssid", info.network.ssid);
    mp_buffer_appendf(&body,
        ",\"wifi_signal_percent\":%d,\"wifi_signal_dbm\":%d,\"wifi_signal_available\":%s"
        ",\"ntp_synchronized\":%s,\"system_time_valid\":%s,\"cpu_temperature_c\":%.1f"
        ",\"storage_free_bytes\":%llu,\"storage_used_bytes\":%llu,\"storage_total_bytes\":%llu"
        ",\"music_bytes\":%llu,\"music_files\":%llu,\"fonts_bytes\":%llu,\"fonts_files\":%llu"
        ",\"config_bytes\":%llu,\"config_files\":%llu"
        ",\"api_healthy\":%s,\"core_healthy\":%s,\"oled_ok\":%s,\"touch_ok\":%s"
        ",\"last_successful_alarm\":%lld,\"uptime_seconds\":%llu",
        info.network.wifi_signal_percent, info.network.wifi_signal_dbm,
        info.network.wifi_signal_available ? "true" : "false",
        info.network.ntp_synchronized ? "true" : "false", info.network.system_time_valid ? "true" : "false",
        info.cpu_temperature_c, (unsigned long long)info.storage_free_bytes,
        (unsigned long long)info.storage_used_bytes, (unsigned long long)info.storage_total_bytes,
        (unsigned long long)info.music_bytes, (unsigned long long)info.music_files,
        (unsigned long long)info.fonts_bytes, (unsigned long long)info.fonts_files,
        (unsigned long long)info.config_bytes, (unsigned long long)info.config_files,
        info.api_healthy ? "true" : "false", info.core_healthy ? "true" : "false",
        info.oled_ok ? "true" : "false", info.touch_ok ? "true" : "false",
        info.last_successful_alarm, (unsigned long long)info.uptime_seconds);
    diag_append_json_string(&body, "next_alarm_text", info.next_alarm_text);
    diag_append_json_string(&body, "room_sensor_status", info.room_sensor_status);
    mp_buffer_appendf(&body, ",\"room_temperature_c\":%.1f,\"room_humidity_percent\":%.1f,\"room_measured_at\":%lld",
                      info.room_temperature_c, info.room_humidity_percent, info.room_measured_at);
    diag_append_json_string(&body, "room_sensor_error", info.room_sensor_error);
    diag_append_json_string(&body, "weather_location", info.weather_location);
    diag_append_json_string(&body, "weather_warning", info.weather_warning);
    mp_buffer_appendf(&body, ",\"weather_observed_at\":%lld,\"weather_status_available\":%s",
                      info.weather_observed_at, info.weather_status_available ? "true" : "false");
    diag_append_json_string(&body, "weather_source_url", info.weather_source_url);
    diag_append_json_string(&body, "weather_result", info.weather_result);
    diag_append_json_string(&body, "weather_message", info.weather_message);
    diag_append_json_string(&body, "weather_run_at", info.weather_run_at);
    diag_append_json_string(&body, "os_pretty_name", info.os_pretty_name);
    diag_append_json_string(&body, "os_version_id", info.os_version_id);
    diag_append_json_string(&body, "os_codename", info.os_codename);
    diag_append_json_string(&body, "kernel_release", info.kernel_release);
    diag_append_json_string(&body, "architecture", info.architecture);
    diag_append_json_string(&body, "hardware_model", info.hardware_model);
    diag_append_json_string(&body, "pi_serial", info.pi_serial);
    diag_append_json_string(&body, "board_revision", info.board_revision);
    diag_append_json_string(&body, "machine_id", info.machine_id);
    diag_append_json_string(&body, "inventory_id", info.inventory_id);
    diag_append_json_string(&body, "cpu_signature", info.cpu_signature);
    diag_append_json_string(&body, "root_device", info.root_device);
    diag_append_json_string(&body, "root_disk", info.root_disk);
    diag_append_json_string(&body, "root_filesystem", info.root_filesystem);
    mp_buffer_appendf(&body, ",\"root_read_only\":%s", info.root_read_only ? "true" : "false");
    diag_append_json_string(&body, "boot_device", info.boot_device);
    diag_append_json_string(&body, "boot_filesystem", info.boot_filesystem);
    diag_append_json_string(&body, "boot_mount_point", info.boot_mount_point);
    mp_buffer_appendf(&body, ",\"sd_present\":%s,\"sd_capacity_bytes\":%llu",
                      info.sd_present ? "true" : "false", (unsigned long long)info.sd_capacity_bytes);
    diag_append_json_string(&body, "sd_device", info.sd_device);
    diag_append_json_string(&body, "sd_type", info.sd_type);
    diag_append_json_string(&body, "sd_name", info.sd_name);
    diag_append_json_string(&body, "sd_manufacturer_id", info.sd_manufacturer_id);
    diag_append_json_string(&body, "sd_oem_id", info.sd_oem_id);
    diag_append_json_string(&body, "sd_serial", info.sd_serial);
    diag_append_json_string(&body, "sd_manufacture_date", info.sd_manufacture_date);
    diag_append_json_string(&body, "sd_cid", info.sd_cid);
    mp_buffer_append(&body, "}");
    return queue_json_builder(connection, 200, &body);
}

static enum MHD_Result queue_diagnostic_download(struct MHD_Connection *connection, char *body,
                                                  size_t body_len, const char *filename) {
    struct MHD_Response *response = MHD_create_response_from_buffer(body_len, body, MHD_RESPMEM_MUST_FREE);
    if (!response) { free(body); return MHD_NO; }
    (void)add_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/plain; charset=utf-8");
    char disposition[256];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", filename);
    (void)add_header(response, "Content-Disposition", disposition);
    add_api_headers(connection, response);
    enum MHD_Result result = MHD_queue_response(connection, 200, response);
    MHD_destroy_response(response);
    return result;
}

static enum MHD_Result serve_diagnostic_report(struct MHD_Connection *connection) {
    struct adult_diagnostic_info info;
    collect_adult_diagnostics(&info);
    struct mp_buffer report;
    if (mp_buffer_init(&report, 4096, 65536) != 0)
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"allocation failed\"}");
    time_t now = time(NULL); struct tm tmv; char generated[64];
    localtime_r(&now, &tmv); strftime(generated, sizeof(generated), "%Y-%m-%d %H:%M:%S %Z", &tmv);
    char last_alarm[64] = "Never";
    if (info.last_successful_alarm > 0) {
        time_t value = (time_t)info.last_successful_alarm; struct tm alarm_tm;
        localtime_r(&value, &alarm_tm); strftime(last_alarm, sizeof(last_alarm), "%Y-%m-%d %H:%M:%S %Z", &alarm_tm);
    }
    mp_buffer_appendf(&report,
        "mk-clock-adult Diagnostic Report\nGenerated: %s\nProduct: %s\nAPI: %s\nWeather: %s\nCompiled: %s\n\n"
        "Platform\nHardware: %s\nOperating system: %s\nOS version: %s\nOS codename: %s\nKernel: %s\nArchitecture: %s\nUptime: %llu seconds\n\n"
        "Device identity\nInventory ID: %s\nRaspberry Pi serial: %s\nBoard revision: %s\nOS machine ID: %s\nCPU signature: %s\n\n"
        "Network and time\nHostname: %s\nInterface: %s\nIP address: %s\nSSID: %s\nWi-Fi signal: %s\nNTP synchronized: %s\nSystem time valid: %s\n\n"
        "Storage\nSystem drive: %s\nRoot partition: %s\nRoot filesystem: %s\nRoot state: %s\nUsed: %llu bytes\nAvailable: %llu bytes\nTotal: %llu bytes\n"
        "Music: %llu bytes in %llu files\nFonts: %llu bytes in %llu files\nConfiguration: %llu bytes in %llu files\n"
        "Boot partition: %s\nBoot filesystem: %s\nBoot mount point: %s\n\n"
        "SD card\nPresent: %s\nDevice: %s\nType: %s\nProduct: %s\nManufacturer ID: %s\nOEM ID: %s\nSerial: %s\nManufactured: %s\nCapacity: %llu bytes\nCID: %s\n\n"
        "Health\nCPU temperature: %.1f C\nAPI: %s\nCore: %s\nOLED: %s\nTouch: %s\nNext alarm: %s\nLast successful alarm: %s\n\n"
        "Inside sensor\nStatus: %s\nTemperature: %.1f C\nHumidity: %.1f%%\nMeasured epoch: %lld\nError: %s\n\n"
        "Weather service\nSource: %s\nLast result: %s\nLast run: %s\nMessage: %s\nDisplay location: %s\nObserved epoch: %lld\nActive warning: %s\n",
        generated, PRODUCT_VERSION, API_VERSION, MP_WEATHER_VERSION, BUILD_TIMESTAMP,
        info.hardware_model[0]?info.hardware_model:"Unavailable", info.os_pretty_name[0]?info.os_pretty_name:"Unavailable",
        info.os_version_id[0]?info.os_version_id:"Unavailable", info.os_codename[0]?info.os_codename:"Unavailable",
        info.kernel_release[0]?info.kernel_release:"Unavailable", info.architecture[0]?info.architecture:"Unavailable",
        (unsigned long long)info.uptime_seconds, info.inventory_id[0]?info.inventory_id:"Unavailable",
        info.pi_serial[0]?info.pi_serial:"Unavailable", info.board_revision[0]?info.board_revision:"Unavailable",
        info.machine_id[0]?info.machine_id:"Unavailable", info.cpu_signature[0]?info.cpu_signature:"Unavailable",
        info.network.hostname, info.network.interface_name[0]?info.network.interface_name:"Unavailable",
        info.network.ip_address[0]?info.network.ip_address:"Unavailable", info.network.ssid[0]?info.network.ssid:"Unavailable",
        info.network.wifi_signal_available ? "Available" : "Unavailable",
        info.network.ntp_synchronized?"Yes":"No", info.network.system_time_valid?"Yes":"No",
        info.root_disk[0]?info.root_disk:"Unavailable", info.root_device[0]?info.root_device:"Unavailable",
        info.root_filesystem[0]?info.root_filesystem:"Unavailable", info.root_device[0]?(info.root_read_only?"Read-only":"Read/write"):"Unavailable",
        (unsigned long long)info.storage_used_bytes, (unsigned long long)info.storage_free_bytes,
        (unsigned long long)info.storage_total_bytes, (unsigned long long)info.music_bytes,
        (unsigned long long)info.music_files, (unsigned long long)info.fonts_bytes, (unsigned long long)info.fonts_files,
        (unsigned long long)info.config_bytes, (unsigned long long)info.config_files,
        info.boot_device[0]?info.boot_device:"Unavailable", info.boot_filesystem[0]?info.boot_filesystem:"Unavailable",
        info.boot_mount_point[0]?info.boot_mount_point:"Unavailable", info.sd_present?"Yes":"No",
        info.sd_device[0]?info.sd_device:"Unavailable", info.sd_type[0]?info.sd_type:"Unavailable",
        info.sd_name[0]?info.sd_name:"Unavailable", info.sd_manufacturer_id[0]?info.sd_manufacturer_id:"Unavailable",
        info.sd_oem_id[0]?info.sd_oem_id:"Unavailable", info.sd_serial[0]?info.sd_serial:"Unavailable",
        info.sd_manufacture_date[0]?info.sd_manufacture_date:"Unavailable", (unsigned long long)info.sd_capacity_bytes,
        info.sd_cid[0]?info.sd_cid:"Unavailable", info.cpu_temperature_c,
        info.api_healthy?"Working":"Unavailable", info.core_healthy?"Working":"Unavailable",
        info.oled_ok?"Working":"Unavailable", info.touch_ok?"Working":"Unavailable",
        info.next_alarm_text[0]?info.next_alarm_text:"No alarm scheduled", last_alarm,
        info.room_sensor_status[0]?info.room_sensor_status:"Unavailable", info.room_temperature_c,
        info.room_humidity_percent, info.room_measured_at, info.room_sensor_error[0]?info.room_sensor_error:"None",
        info.weather_source_url[0]?info.weather_source_url:"Unavailable", info.weather_result[0]?info.weather_result:"Unavailable",
        info.weather_run_at[0]?info.weather_run_at:"Unavailable", info.weather_message[0]?info.weather_message:"Unavailable",
        info.weather_location[0]?info.weather_location:"Unavailable", info.weather_observed_at,
        info.weather_warning[0]?info.weather_warning:"None");
    size_t length = 0; char *body = mp_buffer_steal(&report, &length); mp_buffer_free(&report);
    if (!body) return queue_json(connection, 500, "{\"ok\":false,\"error\":\"report could not be created\"}");
    return queue_diagnostic_download(connection, body, length, "mk-clock-adult-diagnostic-report.txt");
}

static enum MHD_Result dispatch_route(struct MHD_Connection *connection,
                                      struct request_context *context) {
    switch (context->route->id) {
        case ROUTE_STATUS:
        case ROUTE_HEALTH:
            return call_core(connection, MP_IPC_OP_STATUS, NULL, 0);
        case ROUTE_DIAGNOSTICS:
            return serve_network_diagnostics(connection);
        case ROUTE_DIAGNOSTIC_REPORT:
            return serve_diagnostic_report(connection);
        case ROUTE_BACKUP_DOWNLOAD:
            return download_backup(connection);
        case ROUTE_BACKUP_RESTORE:
            return restore_backup(connection, context);
        case ROUTE_FONTS_LIST:
            return serve_fonts_list(connection);
        case ROUTE_MUSIC_LIST:
            return serve_music_list(connection);
        case ROUTE_MUSIC_JOBS:
            return serve_music_jobs(connection);
        case ROUTE_CLEAR_MUSIC_QUEUE:
            return clear_music_queue(connection);
        case ROUTE_FONT_FILE:
            return serve_font_file(connection);
        case ROUTE_UPLOAD_MUSIC:
            return upload_music(connection, context);
        case ROUTE_UPLOAD_FONT:
            return upload_font(connection, context);
        case ROUTE_DELETE_MUSIC:
            return delete_music(connection, context);
        case ROUTE_DELETE_ALL_MUSIC:
            return delete_all_music(connection);
        case ROUTE_DELETE_FONT:
            return delete_font(connection, context);
        case ROUTE_DISPLAY_ACTION: {
            struct mp_ipc_display_action request;
            memset(&request, 0, sizeof(request));
            const char *action = form_value(context, "do");
            if (action && strcmp(action, "clock") == 0) request.action = MP_IPC_ACTION_CLOCK;
            else if (action && strcmp(action, "clear") == 0) request.action = MP_IPC_ACTION_CLEAR;
            else if (action && strcmp(action, "stop") == 0) request.action = MP_IPC_ACTION_STOP_AUDIO;
            else if (action && strcmp(action, "play-music") == 0) request.action = MP_IPC_ACTION_PLAY_MUSIC;
            else return queue_json(connection, 400, "{\"ok\":false,\"error\":\"unknown display action\"}");
            mp_safe_str(request.file, sizeof(request.file), form_value(context, "file"));
            return call_core(connection, MP_IPC_OP_DISPLAY_ACTION, &request, sizeof(request));
        }
        case ROUTE_DISPLAY_PREVIEW:
            return call_core(connection, MP_IPC_OP_DISPLAY_PREVIEW, NULL, 0);
        case ROUTE_BRIGHTNESS_PREVIEW: {
            int percent = form_int(context, "brightness_percent", 35);
            int hold_seconds = form_int(context, "hold_seconds", 8);
            if (percent < 0 || percent > 100 || hold_seconds < 1 || hold_seconds > 30)
                return queue_json(connection, 400, "{\"ok\":false,\"error\":\"invalid brightness preview\"}");
            struct mp_ipc_brightness_preview request;
            memset(&request, 0, sizeof(request));
            request.percent = (uint8_t)percent;
            request.hold_seconds = (uint8_t)hold_seconds;
            return call_core(connection, MP_IPC_OP_BRIGHTNESS_PREVIEW, &request, sizeof(request));
        }
        case ROUTE_CONFIG_ALARM: {
            struct mp_ipc_alarm_config request;
            memset(&request, 0, sizeof(request));
            request.id = (uint8_t)form_int(context, "id", 1);
            request.enabled = (uint8_t)(form_int(context, "enabled", 0) != 0);
            int hour = 7, minute = 0;
            (void)parse_time_value(form_value(context, "time"), &hour, &minute);
            request.hour = (uint8_t)hour;
            request.minute = (uint8_t)minute;
            for (int day = 0; day < 7; day++) {
                char key[16];
                snprintf(key, sizeof(key), "day%d", day);
                if (form_value(context, key)) request.weekdays |= (uint8_t)(1u << day);
            }
            request.start_volume = (uint8_t)form_int(context, "start_volume", 20);
            request.end_volume = (uint8_t)form_int(context, "end_volume", 80);
            mp_safe_str(request.music_file, sizeof(request.music_file), form_value(context, "music_file"));
            return call_core(connection, MP_IPC_OP_CONFIG_ALARM, &request, sizeof(request));
        }
        case ROUTE_CONFIG_AUDIO: {
            struct mp_ipc_audio_config request;
            memset(&request, 0, sizeof(request));
            if (form_value(context, "global_volume") != NULL) {
                request.present_mask |= MP_IPC_AUDIO_GLOBAL_VOLUME;
                request.global_volume = (uint8_t)form_int(context, "global_volume", 80);
            }
            if (request.present_mask == 0)
                return queue_json(connection, 400, "{\"ok\":false,\"error\":\"no audio settings supplied\"}");
            return call_core(connection, MP_IPC_OP_CONFIG_AUDIO, &request, sizeof(request));
        }
        case ROUTE_CONFIG_PERSONALIZATION: {
            struct mp_ipc_personalization_config request;
            memset(&request, 0, sizeof(request));
            mp_safe_str(request.clock_name, sizeof(request.clock_name), form_value(context, "clock_name"));
            return call_core(connection, MP_IPC_OP_CONFIG_PERSONALIZATION, &request, sizeof(request));
        }
        case ROUTE_CONFIG_DISPLAY: {
            struct mp_ipc_display_config request;
            memset(&request, 0, sizeof(request));
            if (form_value(context, "oled_font")) {
                request.present_mask |= MP_IPC_DISPLAY_FONT;
                request.oled_font = (uint8_t)form_int(context, "oled_font", 0);
            }
            if (form_value(context, "oled_font_size")) {
                request.present_mask |= MP_IPC_DISPLAY_FONT_SIZE;
                request.oled_font_size = (uint8_t)form_int(context, "oled_font_size", 48);
            }
            if (form_value(context, "oled_font_file") != NULL) {
                request.present_mask |= MP_IPC_DISPLAY_FONT_FILE;
                mp_safe_str(request.oled_font_file, sizeof(request.oled_font_file), form_value(context, "oled_font_file"));
            }
            if (form_value(context, "inside_font_file") != NULL) {
                request.present_mask |= MP_IPC_DISPLAY_INSIDE_FONT_FILE;
                mp_safe_str(request.inside_font_file, sizeof(request.inside_font_file), form_value(context, "inside_font_file"));
            }
            if (form_value(context, "bedtime_enabled")) {
                request.present_mask |= MP_IPC_DISPLAY_BEDTIME_ENABLED;
                request.bedtime_enabled = (uint8_t)(form_int(context, "bedtime_enabled", 0) != 0);
            }
            if (form_value(context, "bedtime_dim_percent")) {
                request.present_mask |= MP_IPC_DISPLAY_BEDTIME_DIM;
                request.bedtime_dim_percent = (uint8_t)form_int(context, "bedtime_dim_percent", 35);
            }
            if (form_value(context, "weather_warning_chime_enabled") != NULL) {
                request.present_mask |= MP_IPC_DISPLAY_WARNING_CHIME_ENABLED;
                request.weather_warning_chime_enabled =
                    (uint8_t)(form_int(context, "weather_warning_chime_enabled", 1) != 0);
            }
            if (form_value(context, "weather_warning_chime_during_bedtime") != NULL) {
                request.present_mask |= MP_IPC_DISPLAY_WARNING_CHIME_BEDTIME;
                request.weather_warning_chime_during_bedtime =
                    (uint8_t)(form_int(context, "weather_warning_chime_during_bedtime", 0) != 0);
            }
            if (form_value(context, "clock_24h_mode")) {
                request.present_mask |= MP_IPC_DISPLAY_CLOCK_MODE;
                request.clock_24h_mode = (uint8_t)(form_int(context, "clock_24h_mode", 0) != 0);
            }
            if (form_value(context, "oled_color")) {
                const char *color = form_value(context, "oled_color");
                request.present_mask |= MP_IPC_DISPLAY_OLED_COLOR;
                request.oled_color = (uint8_t)(strcmp(color, "yellow") == 0 ? MP_OLED_COLOR_YELLOW :
                    strcmp(color, "white") == 0 ? MP_OLED_COLOR_WHITE : MP_OLED_COLOR_GREEN);
            }
            int hour, minute;
            if (parse_time_value(form_value(context, "bedtime_start"), &hour, &minute) == 0) {
                request.present_mask |= MP_IPC_DISPLAY_BEDTIME_START;
                request.bedtime_start_hour = (uint8_t)hour;
                request.bedtime_start_minute = (uint8_t)minute;
            }
            if (parse_time_value(form_value(context, "bedtime_end"), &hour, &minute) == 0) {
                request.present_mask |= MP_IPC_DISPLAY_BEDTIME_END;
                request.bedtime_end_hour = (uint8_t)hour;
                request.bedtime_end_minute = (uint8_t)minute;
            }
            return call_core(connection, MP_IPC_OP_CONFIG_DISPLAY, &request, sizeof(request));
        }
        case ROUTE_WEATHER_ACTIVITY_GET:
            return weather_activity_get(connection);
        case ROUTE_WEATHER_FRAMES_GET:
            return weather_frames_get(connection);
        case ROUTE_WEATHER_FRAMES_SET:
            return weather_frames_set(connection, context);
        case ROUTE_WEATHER_SOURCE_GET:
            return weather_source_get(connection);
        case ROUTE_WEATHER_SOURCE_SET:
            return weather_source_set(connection, context);
        case ROUTE_WEATHER_UPDATE: {
            struct mp_ipc_weather_update request;
            memset(&request, 0, sizeof(request));
            mp_safe_str(request.location, sizeof(request.location), form_value(context, "location"));

            int requested_warning_count = form_int(context, "warning_count", -1);
            int warning_count = 0;
            if (requested_warning_count >= 0) {
                if (requested_warning_count > MP_WEATHER_WARNING_SLOTS)
                    requested_warning_count = MP_WEATHER_WARNING_SLOTS;
                for (int i = 0; i < requested_warning_count; i++) {
                    char key[48];
                    snprintf(key, sizeof(key), "warning%d_description", i);
                    const char *description = form_value(context, key);
                    if (!description || !*description) continue;
                    mp_safe_str(
                        request.warning_descriptions[warning_count],
                        sizeof(request.warning_descriptions[warning_count]),
                        description
                    );
                    warning_count++;
                }
            }
            if (warning_count == 0) {
                const char *legacy_warning = form_value(context, "warning_type");
                if (legacy_warning && *legacy_warning) {
                    mp_safe_str(
                        request.warning_descriptions[0],
                        sizeof(request.warning_descriptions[0]),
                        legacy_warning
                    );
                    warning_count = 1;
                }
            }
            request.warning_count = (uint8_t)warning_count;

            int current_temperature_c = form_int(context, "current_temperature_c", 0);
            int current_temperature_available = form_int(context, "current_temperature_available", 0);
            int current_temperature_is_forecast = form_int(context, "current_temperature_is_forecast", 0);
            int humidity_percent = form_int(context, "humidity_percent", 0);
            int humidity_available = form_int(context, "humidity_available", 0);
            int precipitation_probability_percent = form_int(context, "precipitation_probability_percent", 0);
            int uv_index = form_int(context, "uv_index", 0);
            request.observed_at = form_u64(context, "observed_at", (uint64_t)time(NULL));

            int slot_temperatures[MP_WEATHER_FORECAST_SLOTS] = {0};
            int slot_temperature_available[MP_WEATHER_FORECAST_SLOTS] = {0};
            int slot_low_temperatures[MP_WEATHER_FORECAST_SLOTS] = {0};
            int slot_low_temperature_available[MP_WEATHER_FORECAST_SLOTS] = {0};
            int slot_low_hours[MP_WEATHER_FORECAST_SLOTS] = {0};
            int slot_high_temperatures[MP_WEATHER_FORECAST_SLOTS] = {0};
            int slot_high_temperature_available[MP_WEATHER_FORECAST_SLOTS] = {0};
            int slot_high_hours[MP_WEATHER_FORECAST_SLOTS] = {0};
            int slot_precipitation[MP_WEATHER_FORECAST_SLOTS] = {0};
            for (int i = 0; i < MP_WEATHER_FORECAST_SLOTS; i++) {
                char key[48];
                snprintf(key, sizeof(key), "slot%d_kind", i);
                int default_kind = i == 0 ? MP_WEATHER_SLOT_ROOM : MP_WEATHER_SLOT_FORECAST;
                int kind = parse_weather_slot_kind(form_value(context, key), default_kind);
                if (kind == 0)
                    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"weather slot kind must be room, outside, forecast, or today\"}");
                request.slots[i].kind = (uint8_t)kind;

                snprintf(key, sizeof(key), "slot%d_label", i);
                mp_safe_str(request.slots[i].label, sizeof(request.slots[i].label), form_value(context, key));
                if (!request.slots[i].label[0])
                    mp_safe_str(request.slots[i].label, sizeof(request.slots[i].label),
                                mp_weather_slot_default_label(kind));

                snprintf(key, sizeof(key), "slot%d_date_label", i);
                mp_safe_str(request.slots[i].date_label, sizeof(request.slots[i].date_label), form_value(context, key));

                snprintf(key, sizeof(key), "slot%d_temperature_available", i);
                slot_temperature_available[i] = form_int(context, key, 0);

                snprintf(key, sizeof(key), "slot%d_temperature_c", i);
                slot_temperatures[i] = form_int(context, key, 0);

                snprintf(key, sizeof(key), "slot%d_low_temperature_available", i);
                slot_low_temperature_available[i] = form_int(context, key, 0);

                snprintf(key, sizeof(key), "slot%d_low_temperature_c", i);
                slot_low_temperatures[i] = form_int(context, key, 0);

                snprintf(key, sizeof(key), "slot%d_low_hour", i);
                slot_low_hours[i] = form_int(context, key, 0);

                snprintf(key, sizeof(key), "slot%d_high_temperature_available", i);
                slot_high_temperature_available[i] = form_int(context, key, 0);

                snprintf(key, sizeof(key), "slot%d_high_temperature_c", i);
                slot_high_temperatures[i] = form_int(context, key, 0);

                snprintf(key, sizeof(key), "slot%d_high_hour", i);
                slot_high_hours[i] = form_int(context, key, 0);

                snprintf(key, sizeof(key), "slot%d_precipitation_probability_percent", i);
                slot_precipitation[i] = form_int(context, key, 0);

                snprintf(key, sizeof(key), "slot%d_icon", i);
                request.slots[i].icon = (uint8_t)parse_weather_icon(form_value(context, key));
            }

            if (current_temperature_c < -99 || current_temperature_c > 99)
                return queue_json(connection, 400, "{\"ok\":false,\"error\":\"current_temperature_c must be between -99 and 99\"}");
            if ((current_temperature_available != 0 && current_temperature_available != 1) ||
                (current_temperature_is_forecast != 0 && current_temperature_is_forecast != 1) ||
                (humidity_available != 0 && humidity_available != 1))
                return queue_json(connection, 400, "{\"ok\":false,\"error\":\"weather availability fields must be 0 or 1\"}");
            if (current_temperature_is_forecast && !current_temperature_available)
                return queue_json(connection, 400, "{\"ok\":false,\"error\":\"forecast temperature source requires an available temperature\"}");
            if (humidity_percent < 0 || humidity_percent > 100 ||
                precipitation_probability_percent < 0 || precipitation_probability_percent > 100 ||
                uv_index < 0 || uv_index > 99)
                return queue_json(connection, 400, "{\"ok\":false,\"error\":\"humidity and precipitation must be 0..100; UV index must be 0..99\"}");
            request.current_temperature_c = (int16_t)current_temperature_c;
            request.current_temperature_available = (uint8_t)current_temperature_available;
            request.current_temperature_is_forecast = (uint8_t)current_temperature_is_forecast;
            request.humidity_percent = (uint8_t)humidity_percent;
            request.humidity_available = (uint8_t)humidity_available;
            request.precipitation_probability_percent = (uint8_t)precipitation_probability_percent;
            request.uv_index = (uint8_t)uv_index;
            for (int i = 0; i < MP_WEATHER_FORECAST_SLOTS; i++) {
                if (slot_temperature_available[i] != 0 && slot_temperature_available[i] != 1)
                    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"weather slot availability fields must be 0 or 1\"}");
                if ((slot_low_temperature_available[i] != 0 && slot_low_temperature_available[i] != 1) ||
                    (slot_high_temperature_available[i] != 0 && slot_high_temperature_available[i] != 1))
                    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"daily weather availability fields must be 0 or 1\"}");
                if (slot_temperatures[i] < -99 || slot_temperatures[i] > 99)
                    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"forecast temperatures must be between -99 and 99\"}");
                if (slot_low_temperatures[i] < -99 || slot_low_temperatures[i] > 99 ||
                    slot_high_temperatures[i] < -99 || slot_high_temperatures[i] > 99)
                    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"daily low and high temperatures must be between -99 and 99\"}");
                if (slot_low_hours[i] < 0 || slot_low_hours[i] > 23 ||
                    slot_high_hours[i] < 0 || slot_high_hours[i] > 23)
                    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"daily low and high hours must be between 0 and 23\"}");
                if (slot_precipitation[i] < 0 || slot_precipitation[i] > 100)
                    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"forecast precipitation must be between 0 and 100\"}");
                request.slots[i].temperature_c = (int16_t)slot_temperatures[i];
                request.slots[i].temperature_available = (uint8_t)slot_temperature_available[i];
                request.slots[i].low_temperature_c = (int16_t)slot_low_temperatures[i];
                request.slots[i].low_temperature_available =
                    (uint8_t)slot_low_temperature_available[i];
                request.slots[i].low_hour = (uint8_t)slot_low_hours[i];
                request.slots[i].high_temperature_c = (int16_t)slot_high_temperatures[i];
                request.slots[i].high_temperature_available =
                    (uint8_t)slot_high_temperature_available[i];
                request.slots[i].high_hour = (uint8_t)slot_high_hours[i];
                request.slots[i].precipitation_probability_percent = (uint8_t)slot_precipitation[i];
            }

            return call_core(connection, MP_IPC_OP_WEATHER_UPDATE, &request, sizeof(request));
        }
        case ROUTE_LOGS:
            return call_core(connection, MP_IPC_OP_LOGS_GET, NULL, 0);
        case ROUTE_LOGS_CLEAR:
            return call_core(connection, MP_IPC_OP_LOGS_CLEAR, NULL, 0);
    }
    return queue_json(connection, 404, "{\"ok\":false,\"error\":\"route not found\"}");
}

static enum MHD_Result serve_local_api(struct MHD_Connection *connection, const char *method,
                                       const char *path, int *handled) {
    *handled = 0;
    if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) return MHD_YES;
    if (strcmp(path, "/api/v1") == 0 || strcmp(path, "/api/v1/") == 0) {
        *handled = 1;
        char discovery[512];
        snprintf(discovery, sizeof(discovery),
            "{\"name\":\"mk-clock-adult API\",\"api_version\":\"" API_VERSION "\","
            "\"product_version\":\"%s\",\"http_engine\":\"libmicrohttpd\","
            "\"core_protocol\":\"binary-ipc-v%u\","
            "\"status\":\"/api/v1/status\",\"capabilities\":\"/api/v1/capabilities\","
            "\"diagnostics\":\"/api/v1/diagnostics\",\"weather\":\"/api/v1/weather\","
            "\"openapi\":\"/api/v1/openapi.json\"}",
            PRODUCT_VERSION, (unsigned int)MP_IPC_VERSION);
        return queue_json(connection, 200, discovery);
    }
    if (strcmp(path, "/api/v1/capabilities") == 0) {
        *handled = 1;
        return queue_json(connection, 200,
            "{\"ok\":true,\"api_version\":\"" API_VERSION "\",\"capabilities\":["
            "\"status.read\",\"diagnostics.network.read\",\"display.control\",\"display.preview\",\"display.brightness.preview\",\"weather.activity.read\",\"weather.source.read\",\"weather.source.write\",\"weather.update\","
            "\"alarm.configure\",\"audio.configure\","
            "\"audio.metadata\",\"audio.optimize\",\"audio.processing-status\",\"audio.queue-clear\","
            "\"touch.input\","
            "\"assets.read\",\"assets.upload\",\"assets.delete\",\"display.color\","
            "\"network.open-controls\",\"logs.read\",\"backup.download\",\"backup.restore\"]}");
    }
    return MHD_YES;
}

static enum MHD_Result request_handler(void *cls, struct MHD_Connection *connection,
                                       const char *url, const char *method, const char *version,
                                       const char *upload_data, size_t *upload_data_size,
                                       void **con_cls) {
    (void)cls;
    (void)version;
    struct request_context *context = *con_cls;
    if (!context) {
        context = calloc(1, sizeof(*context));
        if (!context) return MHD_NO;
        context->route = find_route(method, url);
        context->upload_limit = route_upload_limit(context->route);
        if (context->route && strcmp(method, MHD_HTTP_METHOD_POST) == 0) {
            const char *content_type = MHD_lookup_connection_value(
                connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_TYPE);
            int form_body = content_type &&
                (strncasecmp(content_type, "multipart/form-data", 19) == 0 ||
                 strncasecmp(content_type, "application/x-www-form-urlencoded", 33) == 0);
            if (form_body) {
                context->post = MHD_create_post_processor(connection, 32768, post_iterator, context);
                if (!context->post) context->parse_failed = 1;
            }
        }
        *con_cls = context;
        return MHD_YES;
    }
    if (context->response_queued) return MHD_YES;
    if (*upload_data_size > 0) {
        if (*upload_data_size > MAX_REQUEST_BODY - context->received_bytes) {
            context->body_too_large = 1;
        } else {
            context->received_bytes += *upload_data_size;
            if (!context->parse_failed && context->post &&
                MHD_post_process(context->post, upload_data, *upload_data_size) == MHD_NO)
                context->parse_failed = 1;
        }
        *upload_data_size = 0;
        return MHD_YES;
    }

    context->response_queued = 1;
    if (context->post) {
        MHD_destroy_post_processor(context->post);
        context->post = NULL;
    }
    close_uploads(context);

    if (context->body_too_large)
        return queue_json(connection, MHD_HTTP_CONTENT_TOO_LARGE,
                          "{\"ok\":false,\"error\":\"request or file exceeds its configured limit\"}");
    if (context->parse_failed)
        return queue_json(connection, MHD_HTTP_BAD_REQUEST,
                          "{\"ok\":false,\"error\":\"request body could not be parsed\"}");
    if (strcmp(method, MHD_HTTP_METHOD_OPTIONS) == 0) return queue_options(connection);

    int handled = 0;
    enum MHD_Result result = serve_local_api(connection, method, url, &handled);
    if (handled) return result;
    if (context->route) return dispatch_route(connection, context);
    result = serve_static(connection, method, url, &handled);
    if (handled) return result;
    return queue_json(connection, 404, "{\"ok\":false,\"error\":\"route not found\"}");
}

static void request_completed(void *cls, struct MHD_Connection *connection, void **con_cls,
                              enum MHD_RequestTerminationCode toe) {
    (void)cls;
    (void)connection;
    (void)toe;
    cleanup_context(*con_cls);
    *con_cls = NULL;
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    const char *allowed_origin = getenv("MK_PICLOCK_ALLOWED_ORIGIN");
    if (allowed_origin && *allowed_origin) mp_safe_str(g_allowed_origin, sizeof(g_allowed_origin), allowed_origin);
    g_expected_core_uid = resolve_user_uid("MK_PICLOCK_CORE_USER", "mk-piclock-core");
    g_music_quota_bytes = bytes_from_env("MK_PICLOCK_MUSIC_QUOTA_BYTES", DEFAULT_MUSIC_QUOTA_BYTES);
    g_disk_reserve_bytes = bytes_from_env("MK_PICLOCK_DISK_RESERVE_BYTES", DISK_RESERVE_BYTES);

    (void)mp_asset_ensure_dir(MP_MUSIC_DIR);
    (void)mp_asset_ensure_dir(MP_MUSIC_PROCESS_DIR);
    (void)mp_asset_ensure_dir(MP_FONT_DIR);
    if (mp_music_jobs_start(g_music_quota_bytes, g_disk_reserve_bytes,
                            notify_processed_music, NULL) != 0) {
        fprintf(stderr, "%s: music processing worker could not start\n", API_NAME);
        return 1;
    }

    int public_port = public_port_from_env();
    if (public_port < 0) {
        fprintf(stderr, "Invalid MK_PICLOCK_API_PORT\n");
        mp_music_jobs_stop();
        return 1;
    }
    const char *public_bind = public_bind_from_env();
    struct sockaddr_in bind_address;
    memset(&bind_address, 0, sizeof(bind_address));
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons((uint16_t)public_port);
    if (inet_pton(AF_INET, public_bind, &bind_address.sin_addr) != 1) {
        fprintf(stderr, "Invalid MK_PICLOCK_API_BIND IPv4 address: %s\n", public_bind);
        mp_music_jobs_stop();
        return 1;
    }

    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG,
        (uint16_t)public_port,
        NULL, NULL,
        request_handler, NULL,
        MHD_OPTION_SOCK_ADDR, (struct sockaddr *)&bind_address,
        MHD_OPTION_THREAD_POOL_SIZE, (unsigned int)MHD_THREAD_POOL_SIZE,
        MHD_OPTION_CONNECTION_LIMIT, (unsigned int)MHD_CONNECTION_LIMIT,
        MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)SOCKET_TIMEOUT_SEC,
        MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
        MHD_OPTION_END);
    if (!daemon) {
        fprintf(stderr, "%s: failed to start libmicrohttpd on %s:%d\n", API_NAME, public_bind, public_port);
        mp_music_jobs_stop();
        return 1;
    }
    fprintf(stderr,
        "%s %s listening on %s:%d; HTTP=libmicrohttpd %s; core=binary-ipc-v%u; threads=%d\n",
        API_NAME, API_VERSION, public_bind, public_port, MHD_get_version(),
        (unsigned int)MP_IPC_VERSION, MHD_THREAD_POOL_SIZE);
    while (g_running) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 250000000L};
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR && g_running) {}
    }
    MHD_stop_daemon(daemon);
    mp_music_jobs_stop();
    return 0;
}
