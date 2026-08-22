#ifndef MK_CLOCK_ADULT_HARDWARE_PROFILE_H
#define MK_CLOCK_ADULT_HARDWARE_PROFILE_H

/*
 * mk-clock-adult hardware profile for the Banana Pi M2 Zero on WiFi Builder Debian.
 * gpiochip0 line offsets use Allwinner port numbering.
 */
#define MP_PLATFORM_PROFILE "BPI-M2 Zero"
#define MP_PRODUCT_VERSION "mk-clock-adult-2.3.54-preview59-bpi-m2-zero-r1"

#define MP_OLED_SPI_DEV "/dev/spidev0.0"
#define MP_GPIO_CHIP "/dev/gpiochip0"
#define MP_GPIO_OLED_RST 0   /* PA0, physical pin 13 */
#define MP_GPIO_OLED_DC 2    /* PA2, physical pin 22 */
#define MP_GPIO_TOUCH 17     /* PA17, physical pin 37; legacy builds used PA21 / pin 38 */

#define MP_AHT10_I2C_DEVICE "/dev/i2c-0"
#define MP_AHT10_I2C_ADDRESS 0x38


#define MP_ALSA_CARD_MATCH "MAX98357A"
#define MP_AUDIO_FORCE_STEREO 1

#endif
