#define _GNU_SOURCE

#include "io_helpers.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int mp_write_full(int fd, const void *buffer, size_t length)
{
    if (fd < 0 || (!buffer && length > 0)) {
        errno = EINVAL;
        return -1;
    }
    const unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t written = write(fd, cursor, length);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        cursor += (size_t)written;
        length -= (size_t)written;
    }
    return 0;
}

int mp_fsync_parent_directory(const char *path)
{
    if (!path || !*path) {
        errno = EINVAL;
        return -1;
    }
    char directory[PATH_MAX];
    int written = snprintf(directory, sizeof(directory), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(directory)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    char *slash = strrchr(directory, '/');
    if (!slash) {
        snprintf(directory, sizeof(directory), ".");
    } else if (slash == directory) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }

    int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    int result = fsync(fd);
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return result;
}
