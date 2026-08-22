#include "service_watchdog.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int notify_systemd(const char *message) {
    const char *notify_socket = getenv("NOTIFY_SOCKET");
    if (!notify_socket || !*notify_socket || !message || !*message) return -1;

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    size_t path_len = strlen(notify_socket);
    if (path_len >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }

    socklen_t addr_len;
    if (notify_socket[0] == '@') {
        addr.sun_path[0] = '\0';
        memcpy(addr.sun_path + 1, notify_socket + 1, path_len - 1u);
        addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len);
    } else {
        memcpy(addr.sun_path, notify_socket, path_len + 1u);
        addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1u);
    }

    ssize_t sent = sendto(fd, message, strlen(message), MSG_NOSIGNAL,
                          (const struct sockaddr *)&addr, addr_len);
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return sent >= 0 ? 0 : -1;
}

void mp_service_watchdog_init(struct mp_service_watchdog *watchdog) {
    if (!watchdog) return;
    memset(watchdog, 0, sizeof(*watchdog));

    const char *usec_text = getenv("WATCHDOG_USEC");
    if (!usec_text || !*usec_text || !getenv("NOTIFY_SOCKET")) return;

    const char *pid_text = getenv("WATCHDOG_PID");
    if (pid_text && *pid_text) {
        char *end = NULL;
        unsigned long long configured_pid = strtoull(pid_text, &end, 10);
        if (!end || *end != '\0' || configured_pid != (unsigned long long)getpid()) return;
    }

    char *end = NULL;
    unsigned long long usec = strtoull(usec_text, &end, 10);
    if (!end || *end != '\0' || usec < 2000000ULL) return;

    uint64_t interval_ms = (uint64_t)(usec / 2000ULL); /* half WatchdogSec */
    if (interval_ms < 1000u) interval_ms = 1000u;
    watchdog->ping_interval_ms = interval_ms;
    watchdog->last_ping_ms = 0;
    watchdog->enabled = 1;
}

void mp_service_watchdog_ping_if_due(struct mp_service_watchdog *watchdog) {
    if (!watchdog || !watchdog->enabled) return;
    uint64_t now = monotonic_ms();
    if (watchdog->last_ping_ms != 0 &&
        now - watchdog->last_ping_ms < watchdog->ping_interval_ms)
        return;

    if (notify_systemd("WATCHDOG=1") == 0)
        watchdog->last_ping_ms = now;
}
