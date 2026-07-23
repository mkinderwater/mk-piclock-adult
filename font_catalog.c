#define _GNU_SOURCE

#include "font_catalog.h"

#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const system_font_roots[] = {
    "/usr/share/fonts",
    "/usr/local/share/fonts"
};

static int path_is_in_system_font_root(const char *path) {
    if (!path) return 0;
    for (size_t i = 0; i < sizeof(system_font_roots) / sizeof(system_font_roots[0]); i++) {
        size_t root_len = strlen(system_font_roots[i]);
        if (strncmp(path, system_font_roots[i], root_len) == 0 && path[root_len] == '/') return 1;
    }
    return 0;
}

static int has_font_extension(const char *path) {
    const char *dot = strrchr(path ? path : "", '.');
    return dot && (strcasecmp(dot, ".ttf") == 0 || strcasecmp(dot, ".otf") == 0);
}

static uint64_t font_path_hash(const char *path) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)path; p && *p; p++) {
        hash ^= (uint64_t)*p;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void font_key_for_path(const char *path, char *key, size_t key_len) {
    snprintf(key, key_len, MP_SYSTEM_FONT_KEY_PREFIX "%016llx",
             (unsigned long long)font_path_hash(path));
}

int mp_system_font_is_key(const char *value) {
    if (!value || strncmp(value, MP_SYSTEM_FONT_KEY_PREFIX,
                          strlen(MP_SYSTEM_FONT_KEY_PREFIX)) != 0)
        return 0;
    const char *hex = value + strlen(MP_SYSTEM_FONT_KEY_PREFIX);
    if (strlen(hex) != 16) return 0;
    for (const char *p = hex; *p; p++) {
        if (!isxdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

struct scan_context {
    struct mp_system_font_entry *entries;
    int capacity;
    int count;
    const char *wanted_key;
    char *resolved_path;
    size_t resolved_path_len;
    int found;
};

static int scan_font_directory(const char *dir, int depth, struct scan_context *context) {
    if (!dir || !context || depth > 12 || context->found) return context && context->found ? 1 : 0;
    DIR *stream = opendir(dir);
    if (!stream) return 0;

    struct dirent *de;
    while (!context->found && (de = readdir(stream)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        char path[MP_SYSTEM_FONT_PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        if (written <= 0 || (size_t)written >= sizeof(path)) continue;

        struct stat lst;
        if (lstat(path, &lst) != 0) continue;
        if (S_ISDIR(lst.st_mode)) {
            scan_font_directory(path, depth + 1, context);
            continue;
        }
        if (!has_font_extension(path)) continue;

        char *canonical = realpath(path, NULL);
        if (!canonical || !path_is_in_system_font_root(canonical) ||
            strlen(canonical) >= MP_SYSTEM_FONT_PATH_MAX) {
            free(canonical);
            continue;
        }
        struct stat st;
        if (stat(canonical, &st) != 0 || !S_ISREG(st.st_mode) || access(canonical, R_OK) != 0) {
            free(canonical);
            continue;
        }

        char key[MP_SYSTEM_FONT_KEY_MAX];
        font_key_for_path(canonical, key, sizeof(key));
        if (context->wanted_key) {
            if (strcmp(key, context->wanted_key) == 0) {
                snprintf(context->resolved_path, context->resolved_path_len, "%s", canonical);
                context->found = 1;
            }
            free(canonical);
            continue;
        }

        if (context->count >= context->capacity) {
            int next_capacity = context->capacity > 0 ? context->capacity * 2 : 64;
            struct mp_system_font_entry *grown = realloc(
                context->entries, (size_t)next_capacity * sizeof(*grown));
            if (!grown) {
                free(canonical);
                continue;
            }
            context->entries = grown;
            context->capacity = next_capacity;
        }
        struct mp_system_font_entry *entry = &context->entries[context->count++];
        snprintf(entry->key, sizeof(entry->key), "%s", key);
        snprintf(entry->path, sizeof(entry->path), "%s", canonical);
        const char *base = strrchr(canonical, '/');
        snprintf(entry->filename, sizeof(entry->filename), "%s", base ? base + 1 : de->d_name);
        free(canonical);
    }

    closedir(stream);
    return context->found ? 1 : 0;
}

static int compare_font_entries(const void *left, const void *right) {
    const struct mp_system_font_entry *a = left;
    const struct mp_system_font_entry *b = right;
    int by_name = strcasecmp(a->filename, b->filename);
    return by_name ? by_name : strcmp(a->path, b->path);
}

int mp_system_font_scan(struct mp_system_font_entry *entries, int max_entries) {
    if (!entries || max_entries <= 0) return 0;
    struct scan_context context = {0};
    for (size_t i = 0; i < sizeof(system_font_roots) / sizeof(system_font_roots[0]); i++)
        scan_font_directory(system_font_roots[i], 0, &context);

    qsort(context.entries, (size_t)context.count, sizeof(context.entries[0]), compare_font_entries);
    int count = 0;
    for (int i = 0; i < context.count && count < max_entries; i++) {
        int duplicate = 0;
        for (int j = 0; j < count; j++) {
            if (strcmp(entries[j].key, context.entries[i].key) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) entries[count++] = context.entries[i];
    }
    free(context.entries);
    return count;
}

int mp_system_font_find_filename(const char *filename, char *key, size_t key_len,
                                 char *path, size_t path_len) {
    if (!filename || !*filename || !key || key_len == 0 || !path || path_len == 0) return -1;
    key[0] = '\0';
    path[0] = '\0';

    struct mp_system_font_entry *entries = calloc(MP_SYSTEM_FONT_LIST_MAX, sizeof(*entries));
    if (!entries) return -1;

    int count = mp_system_font_scan(entries, MP_SYSTEM_FONT_LIST_MAX);
    int result = -1;
    for (int i = 0; i < count; i++) {
        if (strcasecmp(entries[i].filename, filename) != 0) continue;
        snprintf(key, key_len, "%s", entries[i].key);
        snprintf(path, path_len, "%s", entries[i].path);
        result = 0;
        break;
    }
    free(entries);
    return result;
}

int mp_system_font_resolve(const char *key, char *path, size_t path_len) {
    if (!mp_system_font_is_key(key) || !path || path_len == 0) return -1;
    path[0] = '\0';
    struct scan_context context = {
        .wanted_key = key,
        .resolved_path = path,
        .resolved_path_len = path_len
    };
    for (size_t i = 0; i < sizeof(system_font_roots) / sizeof(system_font_roots[0]) && !context.found; i++)
        scan_font_directory(system_font_roots[i], 0, &context);
    return context.found ? 0 : -1;
}
