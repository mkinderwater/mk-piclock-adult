#define _GNU_SOURCE
#include "podcast_import.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_worker;
static int g_started = 0;
static int g_running = 0;
static int g_triggered = 0;
static uint64_t g_quota_bytes = 0;
static uint64_t g_reserve_bytes = 0;
static mp_podcast_import_notify_callback g_notify = NULL;
static void *g_notify_userdata = NULL;
static struct mp_podcast_import_snapshot g_status;
static struct mp_podcast_import_failure g_failures[MP_ASSET_LIST_MAX];
static unsigned int g_failure_count = 0;

static int podcast_name_compare(const void *a, const void *b) {
    return strcasecmp((const char *)a, (const char *)b);
}

static int podcast_upload_filename_ok(const char *name) {
    if (!name || !name[0]) return 0;
    size_t len = strlen(name);
    if (len >= MP_ASSET_NAME_MAX || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p < 0x20 || *p == 0x7f || *p == '/' || *p == '\\') return 0;
    }
    return mp_asset_has_mp3_ext(name);
}

static int scan_uploads(char files[][MP_ASSET_NAME_MAX], int max_files) {
    DIR *dir = opendir(MP_PODCAST_UPLOAD_DIR);
    if (!dir) return 0;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        if (!podcast_upload_filename_ok(entry->d_name)) continue;
        char path[768];
        int n = snprintf(path, sizeof(path), "%s/%s", MP_PODCAST_UPLOAD_DIR, entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) continue;
        mp_safe_str(files[count++], MP_ASSET_NAME_MAX, entry->d_name);
    }
    closedir(dir);
    if (count > 1) qsort(files, (size_t)count, MP_ASSET_NAME_MAX, podcast_name_compare);
    return count;
}

static void set_progress(unsigned int percent, void *unused) {
    (void)unused;
    pthread_mutex_lock(&g_lock);
    g_status.current_progress = percent;
    pthread_mutex_unlock(&g_lock);
}

static int quota_allows(const char *target, uint64_t output_bytes) {
    uint64_t current = mp_asset_directory_bytes(MP_PODCAST_DIR, MP_ASSET_SCAN_MUSIC_MP3);
    uint64_t replaced = 0;
    struct stat existing;
    if (stat(target, &existing) == 0 && S_ISREG(existing.st_mode) && existing.st_size > 0)
        replaced = (uint64_t)existing.st_size;
    uint64_t retained = current >= replaced ? current - replaced : 0;
    if (output_bytes > g_quota_bytes || retained > g_quota_bytes - output_bytes) return 0;
    return mp_asset_has_free_space(MP_PODCAST_DIR, output_bytes, g_reserve_bytes);
}

static int process_one(const char *file, char *error, size_t error_len) {
    char library_file[MP_ASSET_NAME_MAX];
    mp_asset_sanitize_filename(file, library_file, sizeof(library_file), "podcast.mp3");
    if (!mp_asset_safe_filename(library_file) || !mp_asset_has_mp3_ext(library_file)) {
        mp_safe_str(error, error_len, "podcast filename could not be normalized safely");
        return -1;
    }

    char source[768], target[768], output_template[768];
    if (snprintf(source, sizeof(source), "%s/%s", MP_PODCAST_UPLOAD_DIR, file) >= (int)sizeof(source) ||
        snprintf(target, sizeof(target), "%s/%s", MP_PODCAST_DIR, library_file) >= (int)sizeof(target)) {
        mp_safe_str(error, error_len, "podcast path is too long");
        return -1;
    }
    if (mp_asset_validate_mp3(source) != 0) {
        mp_safe_str(error, error_len, "file is not a readable MP3");
        return -1;
    }

    snprintf(output_template, sizeof(output_template), "%s/.podcast-import-XXXXXX", MP_PODCAST_DIR);
    int fd = mkstemp(output_template);
    if (fd < 0) {
        mp_safe_str(error, error_len, "processed podcast could not be created");
        return -1;
    }
    close(fd);

    /* Speech-first profile: mono, transparent speech bitrate, full voice-band low-pass. */
    const struct mp_audio_optimize_settings settings = {
        .bitrate_kbps = 96,
        .sample_rate_hz = 44100,
        .lowpass_hz = 16000,
        .quality = 2
    };
    if (mp_asset_optimize_mp3(source, output_template, &settings, set_progress, NULL,
                              error, error_len) != 0 || mp_asset_validate_mp3(output_template) != 0) {
        (void)unlink(output_template);
        if (!error[0]) mp_safe_str(error, error_len, "processed podcast failed validation");
        return -1;
    }

    struct stat st;
    if (stat(output_template, &st) != 0 || st.st_size <= 0 ||
        !quota_allows(target, (uint64_t)st.st_size)) {
        (void)unlink(output_template);
        mp_safe_str(error, error_len, "podcast quota or free-space reserve would be exceeded");
        return -1;
    }
    if (rename(output_template, target) != 0) {
        (void)unlink(output_template);
        mp_safe_str(error, error_len, "processed podcast could not be moved into the library");
        return -1;
    }
    (void)chmod(target, 0640);
    if (unlink(source) != 0) {
        mp_safe_str(error, error_len, "podcast imported, but source upload could not be removed");
        return -1;
    }
    if (g_notify && g_notify(library_file, g_notify_userdata) != 0)
        mp_safe_str(error, error_len, "podcast imported, but clock core could not reload the library");
    return 0;
}

static void run_import(void) {
    char files[MP_ASSET_LIST_MAX][MP_ASSET_NAME_MAX];
    int count = scan_uploads(files, MP_ASSET_LIST_MAX);
    pthread_mutex_lock(&g_lock);
    memset(&g_status, 0, sizeof(g_status));
    memset(g_failures, 0, sizeof(g_failures));
    g_failure_count = 0;
    g_status.active = count > 0;
    g_status.total = count > 0 ? (unsigned int)count : 0;
    g_status.waiting = g_status.total;
    pthread_mutex_unlock(&g_lock);

    for (int i = 0; i < count && g_running; i++) {
        pthread_mutex_lock(&g_lock);
        mp_safe_str(g_status.current_file, sizeof(g_status.current_file), files[i]);
        g_status.current_progress = 1;
        pthread_mutex_unlock(&g_lock);

        char error[MP_PODCAST_IMPORT_ERROR_MAX] = "";
        int result = process_one(files[i], error, sizeof(error));

        pthread_mutex_lock(&g_lock);
        if (g_status.waiting > 0) g_status.waiting--;
        if (result == 0) g_status.processed++;
        else {
            g_status.failed++;
            mp_safe_str(g_status.last_error, sizeof(g_status.last_error), error);
            if (g_failure_count < MP_ASSET_LIST_MAX) {
                mp_safe_str(g_failures[g_failure_count].file, sizeof(g_failures[g_failure_count].file), files[i]);
                mp_safe_str(g_failures[g_failure_count].error, sizeof(g_failures[g_failure_count].error), error);
                g_failure_count++;
            }
        }
        g_status.current_progress = result == 0 ? 100u : 0u;
        pthread_mutex_unlock(&g_lock);
    }

    pthread_mutex_lock(&g_lock);
    g_status.active = 0;
    g_status.current_file[0] = '\0';
    g_status.current_progress = 0;
    pthread_mutex_unlock(&g_lock);
}

static void *worker_main(void *unused) {
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&g_lock);
        while (g_running && !g_triggered) pthread_cond_wait(&g_cond, &g_lock);
        if (!g_running) {
            pthread_mutex_unlock(&g_lock);
            break;
        }
        g_triggered = 0;
        pthread_mutex_unlock(&g_lock);
        run_import();
    }
    return NULL;
}

int mp_podcast_import_start(uint64_t quota_bytes, uint64_t reserve_bytes,
                            mp_podcast_import_notify_callback notify, void *userdata) {
    if (g_started) return 0;
    if (mp_asset_ensure_dir(MP_PODCAST_DIR) != 0 || mp_asset_ensure_dir(MP_PODCAST_UPLOAD_DIR) != 0) return -1;
    memset(&g_status, 0, sizeof(g_status));
    g_quota_bytes = quota_bytes;
    g_reserve_bytes = reserve_bytes;
    g_notify = notify;
    g_notify_userdata = userdata;
    g_running = 1;
    if (pthread_create(&g_worker, NULL, worker_main, NULL) != 0) {
        g_running = 0;
        return -1;
    }
    g_started = 1;
    return 0;
}

void mp_podcast_import_stop(void) {
    if (!g_started) return;
    pthread_mutex_lock(&g_lock);
    g_running = 0;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
    pthread_join(g_worker, NULL);
    g_started = 0;
}

int mp_podcast_import_scan(void) {
    char files[MP_ASSET_LIST_MAX][MP_ASSET_NAME_MAX];
    int waiting = scan_uploads(files, MP_ASSET_LIST_MAX);
    pthread_mutex_lock(&g_lock);
    if (!g_started || !g_running) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    if (!g_status.active) {
        g_status.waiting = waiting > 0 ? (unsigned int)waiting : 0;
        g_status.total = g_status.waiting;
        g_status.processed = 0;
        g_status.failed = 0;
        g_status.current_progress = 0;
        g_status.current_file[0] = '\0';
        g_status.last_error[0] = '\0';
        memset(g_failures, 0, sizeof(g_failures));
        g_failure_count = 0;
    }
    pthread_mutex_unlock(&g_lock);
    return waiting;
}

int mp_podcast_import_trigger(void) {
    int waiting = mp_podcast_import_scan();
    if (waiting < 0) return -1;
    pthread_mutex_lock(&g_lock);
    if (!g_status.active && waiting > 0) {
        g_triggered = 1;
        pthread_cond_signal(&g_cond);
    }
    pthread_mutex_unlock(&g_lock);
    return waiting;
}

void mp_podcast_import_snapshot(struct mp_podcast_import_snapshot *snapshot) {
    if (!snapshot) return;
    pthread_mutex_lock(&g_lock);
    *snapshot = g_status;
    pthread_mutex_unlock(&g_lock);
}

int mp_podcast_import_failure_count(void) {
    pthread_mutex_lock(&g_lock);
    unsigned int count = g_failure_count;
    pthread_mutex_unlock(&g_lock);
    return (int)count;
}

int mp_podcast_import_failure_get(unsigned int index, struct mp_podcast_import_failure *failure) {
    if (!failure) return -1;
    pthread_mutex_lock(&g_lock);
    if (index >= g_failure_count) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    *failure = g_failures[index];
    pthread_mutex_unlock(&g_lock);
    return 0;
}
