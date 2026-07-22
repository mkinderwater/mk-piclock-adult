#ifndef MK_PICLOCK_AHT10_SENSOR_H
#define MK_PICLOCK_AHT10_SENSOR_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MP_AHT10_DEFAULT_DEVICE "/dev/i2c-1"
#define MP_AHT10_DEFAULT_ADDRESS 0x38

struct mp_aht10_sample {
    double temperature_c;
    double humidity_percent;
    time_t measured_at;
};

int mp_aht10_decode(const uint8_t frame[6], struct mp_aht10_sample *sample,
                    char *error, size_t error_size);
int mp_aht10_read(const char *device, int address, struct mp_aht10_sample *sample,
                  char *error, size_t error_size);

#endif
