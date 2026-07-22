#define _POSIX_C_SOURCE 200809L

#include "aht10_sensor.h"
#include "compiler_attrs.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define AHT10_STATUS_BUSY 0x80u
#define AHT10_STATUS_CALIBRATED 0x08u
#define AHT10_MEASUREMENT_DELAY_MS 80L
#define AHT10_READY_RETRIES 5

static void set_error(char *error, size_t error_size, const char *format, ...) MP_PRINTF_LIKE(3, 4);

static void set_error(char *error, size_t error_size, const char *format, ...)
{
    if (!error || error_size == 0) return;
    va_list ap;
    va_start(ap, format);
    vsnprintf(error, error_size, format, ap);
    va_end(ap);
}

static void sleep_ms(long milliseconds)
{
    struct timespec request = {
        .tv_sec = milliseconds / 1000L,
        .tv_nsec = (milliseconds % 1000L) * 1000000L
    };
    while (nanosleep(&request, &request) != 0 && errno == EINTR) {}
}

static int write_exact(int fd, const uint8_t *data, size_t size,
                       char *error, size_t error_size)
{
    ssize_t count;
    do {
        count = write(fd, data, size);
    } while (count < 0 && errno == EINTR);

    if (count < 0) {
        set_error(error, error_size, "AHT10 write failed: %s", strerror(errno));
        return -1;
    }
    if ((size_t)count != size) {
        set_error(error, error_size, "AHT10 write was short: %zd of %zu bytes", count, size);
        return -1;
    }
    return 0;
}

static int read_exact(int fd, uint8_t *data, size_t size,
                      char *error, size_t error_size)
{
    ssize_t count;
    do {
        count = read(fd, data, size);
    } while (count < 0 && errno == EINTR);

    if (count < 0) {
        set_error(error, error_size, "AHT10 read failed: %s", strerror(errno));
        return -1;
    }
    if ((size_t)count != size) {
        set_error(error, error_size, "AHT10 read was short: %zd of %zu bytes", count, size);
        return -1;
    }
    return 0;
}

static int read_status(int fd, uint8_t *status, char *error, size_t error_size)
{
    if (read_exact(fd, status, 1, error, error_size) != 0) return -1;
    return 0;
}

static int initialize_sensor(int fd, char *error, size_t error_size)
{
    uint8_t status = 0;
    if (read_status(fd, &status, error, error_size) != 0) return -1;
    if ((status & AHT10_STATUS_CALIBRATED) != 0) return 0;

    const uint8_t aht10_init[3] = {0xE1u, 0x08u, 0x00u};
    if (write_exact(fd, aht10_init, sizeof(aht10_init), error, error_size) != 0)
        return -1;
    sleep_ms(20);

    if (read_status(fd, &status, error, error_size) != 0) return -1;
    if ((status & AHT10_STATUS_CALIBRATED) != 0) return 0;

    /* Some breakout boards marked AHT10 contain the command-compatible AHT20. */
    const uint8_t aht20_init[3] = {0xBEu, 0x08u, 0x00u};
    if (write_exact(fd, aht20_init, sizeof(aht20_init), error, error_size) != 0)
        return -1;
    sleep_ms(20);

    if (read_status(fd, &status, error, error_size) != 0) return -1;
    if ((status & AHT10_STATUS_CALIBRATED) == 0) {
        set_error(error, error_size, "AHT10 calibration did not become ready");
        return -1;
    }
    return 0;
}

int mp_aht10_decode(const uint8_t frame[6], struct mp_aht10_sample *sample,
                    char *error, size_t error_size)
{
    if (!frame || !sample) {
        set_error(error, error_size, "invalid AHT10 decode arguments");
        return -1;
    }
    if ((frame[0] & AHT10_STATUS_BUSY) != 0) {
        set_error(error, error_size, "AHT10 measurement is still busy");
        return -1;
    }
    if ((frame[0] & AHT10_STATUS_CALIBRATED) == 0) {
        set_error(error, error_size, "AHT10 measurement is not calibrated");
        return -1;
    }

    uint32_t raw_humidity = ((uint32_t)frame[1] << 12) |
                            ((uint32_t)frame[2] << 4) |
                            ((uint32_t)frame[3] >> 4);
    uint32_t raw_temperature = ((uint32_t)(frame[3] & 0x0Fu) << 16) |
                               ((uint32_t)frame[4] << 8) |
                               (uint32_t)frame[5];

    double humidity = ((double)raw_humidity * 100.0) / 1048576.0;
    double temperature = ((double)raw_temperature * 200.0) / 1048576.0 - 50.0;
    if (!isfinite(temperature) || !isfinite(humidity) ||
        temperature < -50.0 || temperature > 150.0 ||
        humidity < 0.0 || humidity > 100.0) {
        set_error(error, error_size, "AHT10 returned an invalid measurement");
        return -1;
    }

    sample->temperature_c = temperature;
    sample->humidity_percent = humidity;
    sample->measured_at = time(NULL);
    if (error && error_size) error[0] = '\0';
    return 0;
}

int mp_aht10_read(const char *device, int address, struct mp_aht10_sample *sample,
                  char *error, size_t error_size)
{
    if (!device || !*device || !sample || address < 0x03 || address > 0x77) {
        set_error(error, error_size, "invalid AHT10 configuration");
        return -1;
    }

    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        set_error(error, error_size, "cannot open %s: %s", device, strerror(errno));
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, address) != 0) {
        set_error(error, error_size, "cannot select AHT10 address 0x%02X on %s: %s",
                  address, device, strerror(errno));
        close(fd);
        return -1;
    }

    int result = -1;
    if (initialize_sensor(fd, error, error_size) != 0) goto done;

    const uint8_t trigger[3] = {0xACu, 0x33u, 0x00u};
    if (write_exact(fd, trigger, sizeof(trigger), error, error_size) != 0) goto done;
    sleep_ms(AHT10_MEASUREMENT_DELAY_MS);

    for (int attempt = 0; attempt < AHT10_READY_RETRIES; attempt++) {
        uint8_t frame[6];
        if (read_exact(fd, frame, sizeof(frame), error, error_size) != 0) goto done;
        if ((frame[0] & AHT10_STATUS_BUSY) != 0) {
            sleep_ms(20);
            continue;
        }
        result = mp_aht10_decode(frame, sample, error, error_size);
        goto done;
    }
    set_error(error, error_size, "AHT10 measurement timed out");

done:
    close(fd);
    return result;
}
