#ifndef MK_PICLOCK_PODCAST_IMPORT_H
#define MK_PICLOCK_PODCAST_IMPORT_H

#include <stdint.h>
#include "asset_store.h"

#define MP_PODCAST_UPLOAD_DIR MP_PODCAST_DIR "/upload"
#define MP_PODCAST_IMPORT_ERROR_MAX 160

struct mp_podcast_import_failure {
    char file[MP_ASSET_NAME_MAX];
    char error[MP_PODCAST_IMPORT_ERROR_MAX];
};

struct mp_podcast_import_snapshot {
    int active;
    unsigned int waiting;
    unsigned int processed;
    unsigned int failed;
    unsigned int total;
    unsigned int current_progress;
    char current_file[MP_ASSET_NAME_MAX];
    char last_error[MP_PODCAST_IMPORT_ERROR_MAX];
};

typedef int (*mp_podcast_import_notify_callback)(const char *file, void *userdata);

int mp_podcast_import_start(uint64_t quota_bytes, uint64_t reserve_bytes,
                            mp_podcast_import_notify_callback notify, void *userdata);
void mp_podcast_import_stop(void);
int mp_podcast_import_scan(void);
int mp_podcast_import_trigger(void);
void mp_podcast_import_snapshot(struct mp_podcast_import_snapshot *snapshot);
int mp_podcast_import_failure_count(void);
int mp_podcast_import_failure_get(unsigned int index, struct mp_podcast_import_failure *failure);

#endif
