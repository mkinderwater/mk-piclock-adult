/*
 * mk-piclock-api.c
 *
 * Public libmicrohttpd gateway for mk-piclock.
 * HTTP terminates here. The hardware daemon uses a compact binary IPC protocol.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
#include <sys/timex.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "asset_store.h"
#include "font_catalog.h"
#include "ipc_protocol.h"
#include "music_jobs.h"
#include "util.h"


#include "weather_source_store.h"
#define API_NAME "mk-clock-adult-api"
#define API_VERSION "1.37"
#define PRODUCT_VERSION "mk-clock-adult-1.2.40"
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

static volatile sig_atomic_t g_running = 1;
static char g_allowed_origin[ALLOWED_ORIGIN_MAX];
static uid_t g_expected_core_uid = (uid_t)-1;
static uint64_t g_music_quota_bytes = DEFAULT_MUSIC_QUOTA_BYTES;
static uint64_t g_disk_reserve_bytes = DISK_RESERVE_BYTES;


enum route_id {
    ROUTE_STATUS,
    ROUTE_HEALTH,
    ROUTE_DIAGNOSTICS,
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
        "{\"id\":2,\"name\":\"Pixel\"},{\"id\":3,\"name\":\"Pixel Bold\"}],"
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
        "\"slot1\":{\"mode\":\"%s\",\"offset_hours\":%d},"
        "\"slot2\":{\"mode\":\"%s\",\"offset_hours\":%d},"
        "\"slot3\":{\"mode\":\"%s\",\"offset_hours\":%d}}",
        MP_WEATHER_FRAMES_FILE,
        mp_weather_frame_mode_name(config.slots[0].mode), config.slots[0].offset_hours,
        mp_weather_frame_mode_name(config.slots[1].mode), config.slots[1].offset_hours,
        mp_weather_frame_mode_name(config.slots[2].mode), config.slots[2].offset_hours
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
                "{\"ok\":false,\"error\":\"weather panel mode must be room, outside, or offset\"}"
            );
        }

        snprintf(key, sizeof(key), "slot%d_offset_hours", index + 1);
        if (form_value(context, key))
            config.slots[index].offset_hours =
                form_int(context, key, config.slots[index].offset_hours);
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
        "\"slot1\":{\"mode\":\"%s\",\"offset_hours\":%d},"
        "\"slot2\":{\"mode\":\"%s\",\"offset_hours\":%d},"
        "\"slot3\":{\"mode\":\"%s\",\"offset_hours\":%d}}",
        changed ? "true" : "false",
        mp_weather_frame_mode_name(config.slots[0].mode), config.slots[0].offset_hours,
        mp_weather_frame_mode_name(config.slots[1].mode), config.slots[1].offset_hours,
        mp_weather_frame_mode_name(config.slots[2].mode), config.slots[2].offset_hours
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
    struct network_diagnostics info;
    collect_network_diagnostics(&info);
    struct mp_buffer body;
    if (mp_buffer_init(&body, 512, 4096) != 0)
        return queue_json(connection, 500, "{\"ok\":false,\"error\":\"allocation failed\"}");
    mp_buffer_append(&body, "{\"ok\":true,\"hostname\":\"");
    mp_buffer_append_json_string(&body, info.hostname);
    mp_buffer_append(&body, "\",\"interface\":\"");
    mp_buffer_append_json_string(&body, info.interface_name);
    mp_buffer_append(&body, "\",\"ip_address\":\"");
    mp_buffer_append_json_string(&body, info.ip_address);
    mp_buffer_append(&body, "\",\"ssid\":\"");
    mp_buffer_append_json_string(&body, info.ssid);
    mp_buffer_appendf(&body,
        "\",\"wifi_signal_percent\":%d,\"wifi_signal_dbm\":%d,"
        "\"wifi_signal_available\":%s,\"ntp_synchronized\":%s,"
        "\"system_time_valid\":%s}",
        info.wifi_signal_percent, info.wifi_signal_dbm,
        info.wifi_signal_available ? "true" : "false",
        info.ntp_synchronized ? "true" : "false",
        info.system_time_valid ? "true" : "false");
    return queue_json_builder(connection, 200, &body);
}

static enum MHD_Result dispatch_route(struct MHD_Connection *connection,
                                      struct request_context *context) {
    switch (context->route->id) {
        case ROUTE_STATUS:
        case ROUTE_HEALTH:
            return call_core(connection, MP_IPC_OP_STATUS, NULL, 0);
        case ROUTE_DIAGNOSTICS:
            return serve_network_diagnostics(connection);
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
            mp_safe_str(request.warning_type, sizeof(request.warning_type), form_value(context, "warning_type"));

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
            int slot_precipitation[MP_WEATHER_FORECAST_SLOTS] = {0};
            for (int i = 0; i < MP_WEATHER_FORECAST_SLOTS; i++) {
                char key[48];
                snprintf(key, sizeof(key), "slot%d_kind", i);
                int default_kind = i == 0 ? MP_WEATHER_SLOT_ROOM : MP_WEATHER_SLOT_FORECAST;
                int kind = parse_weather_slot_kind(form_value(context, key), default_kind);
                if (kind == 0)
                    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"weather slot kind must be room, outside, or forecast\"}");
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
                if (slot_temperatures[i] < -99 || slot_temperatures[i] > 99)
                    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"forecast temperatures must be between -99 and 99\"}");
                if (slot_precipitation[i] < 0 || slot_precipitation[i] > 100)
                    return queue_json(connection, 400, "{\"ok\":false,\"error\":\"forecast precipitation must be between 0 and 100\"}");
                request.slots[i].temperature_c = (int16_t)slot_temperatures[i];
                request.slots[i].temperature_available = (uint8_t)slot_temperature_available[i];
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
            "\"network.open-controls\",\"logs.read\"]}");
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
