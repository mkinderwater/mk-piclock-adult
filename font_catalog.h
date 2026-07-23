#ifndef MK_PICLOCK_FONT_CATALOG_H
#define MK_PICLOCK_FONT_CATALOG_H

#include <stddef.h>

#define MP_SYSTEM_FONT_KEY_PREFIX "system:"
#define MP_SYSTEM_FONT_KEY_MAX 32
#define MP_SYSTEM_FONT_PATH_MAX 768
#define MP_SYSTEM_FONT_FILE_MAX 256
#define MP_SYSTEM_FONT_LIST_MAX 512

struct mp_system_font_entry {
    char key[MP_SYSTEM_FONT_KEY_MAX];
    char path[MP_SYSTEM_FONT_PATH_MAX];
    char filename[MP_SYSTEM_FONT_FILE_MAX];
};

int mp_system_font_is_key(const char *value);
int mp_system_font_scan(struct mp_system_font_entry *entries, int max_entries);
int mp_system_font_find_filename(const char *filename, char *key, size_t key_len,
                                 char *path, size_t path_len);
int mp_system_font_resolve(const char *key, char *path, size_t path_len);

#endif
