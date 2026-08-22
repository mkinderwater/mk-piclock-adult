#ifndef MK_CLOCK_ADULT_SERVICE_WATCHDOG_H
#define MK_CLOCK_ADULT_SERVICE_WATCHDOG_H

#include <stdint.h>

struct mp_service_watchdog {
    uint64_t ping_interval_ms;
    uint64_t last_ping_ms;
    int enabled;
};

/* Read systemd WATCHDOG_USEC/WATCHDOG_PID and prepare main-loop heartbeats. */
void mp_service_watchdog_init(struct mp_service_watchdog *watchdog);

/* Send WATCHDOG=1 when half of the configured service interval has elapsed. */
void mp_service_watchdog_ping_if_due(struct mp_service_watchdog *watchdog);

#endif
