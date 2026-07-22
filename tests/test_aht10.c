#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../aht10_sensor.h"

static void encode_sample(double temperature_c, double humidity_percent, uint8_t frame[6])
{
    uint32_t raw_humidity = (uint32_t)lround(humidity_percent * 1048576.0 / 100.0);
    uint32_t raw_temperature = (uint32_t)lround((temperature_c + 50.0) * 1048576.0 / 200.0);
    if (raw_humidity > 0xFFFFFu) raw_humidity = 0xFFFFFu;
    if (raw_temperature > 0xFFFFFu) raw_temperature = 0xFFFFFu;

    frame[0] = 0x08u;
    frame[1] = (uint8_t)(raw_humidity >> 12);
    frame[2] = (uint8_t)(raw_humidity >> 4);
    frame[3] = (uint8_t)((raw_humidity << 4) | (raw_temperature >> 16));
    frame[4] = (uint8_t)(raw_temperature >> 8);
    frame[5] = (uint8_t)raw_temperature;
}

int main(void)
{
    uint8_t frame[6];
    encode_sample(23.5, 45.5, frame);

    struct mp_aht10_sample sample;
    char error[160] = "not cleared";
    assert(mp_aht10_decode(frame, &sample, error, sizeof(error)) == 0);
    assert(fabs(sample.temperature_c - 23.5) < 0.02);
    assert(fabs(sample.humidity_percent - 45.5) < 0.02);
    assert(error[0] == '\0');

    frame[0] |= 0x80u;
    assert(mp_aht10_decode(frame, &sample, error, sizeof(error)) != 0);
    assert(strstr(error, "busy") != NULL);

    encode_sample(20.0, 50.0, frame);
    frame[0] &= (uint8_t)~0x08u;
    assert(mp_aht10_decode(frame, &sample, error, sizeof(error)) != 0);
    assert(strstr(error, "not calibrated") != NULL);

    puts("AHT10 decode tests passed");
    return 0;
}
