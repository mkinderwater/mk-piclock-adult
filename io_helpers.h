#ifndef MK_PICLOCK_IO_HELPERS_H
#define MK_PICLOCK_IO_HELPERS_H

#include <stddef.h>

int mp_write_full(int fd, const void *buffer, size_t length);
int mp_fsync_parent_directory(const char *path);

#endif
