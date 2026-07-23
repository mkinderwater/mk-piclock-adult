#include "../font_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    struct mp_system_font_entry entries[MP_SYSTEM_FONT_LIST_MAX];
    int count = mp_system_font_scan(entries, MP_SYSTEM_FONT_LIST_MAX);
    assert(count >= 0);

    if (count == 0) {
        puts("font catalog test skipped: no readable system fonts");
        return 0;
    }

    assert(mp_system_font_is_key(entries[0].key));
    assert(entries[0].filename[0] != '\0');
    assert(access(entries[0].path, R_OK) == 0);

    char key[MP_SYSTEM_FONT_KEY_MAX];
    char path[MP_SYSTEM_FONT_PATH_MAX];
    assert(mp_system_font_find_filename(entries[0].filename, key, sizeof(key), path, sizeof(path)) == 0);
    assert(strcmp(key, entries[0].key) == 0);
    assert(strcmp(path, entries[0].path) == 0);

    assert(mp_system_font_find_filename("font-that-does-not-exist.ttf", key, sizeof(key), path, sizeof(path)) == -1);
    assert(key[0] == '\0');
    assert(path[0] == '\0');

    puts("font catalog tests passed");
    return 0;
}
