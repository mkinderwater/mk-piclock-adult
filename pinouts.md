# PINOUT.MD — mk-clock-adult 2.3.24

Production wiring reference for the **Banana Pi M2 Zero** adult clock running `bpi-zero-clock 1.0.3`.

Physical pin numbers refer to the 40-pin Banana Pi header.

## SSD1322 OLED — SPI0

| OLED pin | Function | BPI M2 Zero | Physical pin |
|---|---|---|---:|
| VSS / GND | Ground | GND | 6 |
| VCC_IN | 3.3 V | 3.3 V | 1 |
| D0 / CLK | SPI clock | SPI0-CLK / PC2 | 23 |
| D1 / DIN | SPI MOSI | SPI0-MOSI / PC0 | 19 |
| D/C | Data / Command | PA2 | 22 |
| RES | Reset | PA0 | 13 |
| CS | Chip Select | SPI0-CS / PC3 | 24 |

Software device:

```text
/dev/spidev0.0
```

Application GPIO assignments:

```text
OLED RESET = PA0  / gpiochip0 offset 0  / physical pin 13
OLED D/C   = PA2  / gpiochip0 offset 2  / physical pin 22
```

## AHT10 inside temperature / humidity sensor — I2C0

| AHT10 pin | Function | BPI M2 Zero | Physical pin |
|---|---|---|---:|
| VIN / VCC | 3.3 V | 3.3 V | 17 |
| GND | Ground | GND | 9 |
| SDA | I2C data | TWI0-SDA / PA12 | 3 |
| SCL | I2C clock | TWI0-SCK / PA11 | 5 |

Software:

```text
Device:  /dev/i2c-0
Address: 0x38
```

## TTP223B touch sensor

| TTP223B pin | Function | BPI M2 Zero | Physical pin |
|---|---|---|---:|
| VCC | 3.3 V | 3.3 V | 17 |
| GND | Ground | GND | 39 |
| OUT | Touch signal | PA21 | 38 |

Application GPIO assignment:

```text
TOUCH = PA21 / gpiochip0 offset 21 / physical pin 38
```

## MAX98357A I2S amplifier

The MAX98357A hardware path is owned by the `bpi-zero-clock 1.0.3` image and its validated Device Tree / codec driver.

| MAX98357A pin | Function | BPI M2 Zero | Physical pin |
|---|---|---|---:|
| VIN | Amplifier power | 5 V | 2 |
| GND | Ground | GND | 14 |
| BCLK | I2S bit clock | PA19 | 27 |
| LRC / WS | I2S word / frame clock | PA18 | 28 |
| DIN | I2S audio data | PA20 | 40 |
| SD / EN | Codec-driver shutdown / enable | PA1 | 11 |

Audio device:

```text
hw:MAX98357A,0
```

Image-owned audio behavior:

```text
Codec driver:  snd-soc-max98357a
Compatible:    maxim,max98357a
SD / EN GPIO:  PA1
SD delay:      5 ms
MCLK-FS:       256
```

The application does **not** drive PA1 directly.

Local MP3/alarm playback retains forced stereo output (`MPG123_FORCE_STEREO`) so mono material fills both I2S slots.

## 40-pin header summary

```text
 3.3V  (1) (2)  5V ---------------- MAX98357A VIN
 SDA0  (3) (4)  5V
 SCL0  (5) (6)  GND --------------- OLED GND
       (7) (8)
 GND   (9) (10)
 PA1  (11) (12) -------------------- MAX98357A SD/EN
 PA0  (13) (14) GND ---------------- MAX98357A GND
      (15) (16)
 3.3V (17) (18) -------------------- AHT10 / touch VCC
 MOSI (19) (20) GND ---------------- OLED D1
 MISO (21) (22) PA2 ---------------- OLED D/C
 SCLK (23) (24) CS0 ---------------- OLED D0 / CS
 GND  (25) (26)
 PA19 (27) (28) PA18 --------------- MAX98357A BCLK / LRC
      (29) (30) GND
      (31) (32)
      (33) (34) GND
      (35) (36)
      (37) (38) PA21 --------------- TTP223B OUT
 GND  (39) (40) PA20 --------------- Touch GND / MAX98357A DIN
```

## Application-owned vs. image-owned hardware

### Application-owned

- SSD1322 rendering and SPI transactions
- OLED RESET on PA0
- OLED D/C on PA2
- TTP223B input on PA21
- AHT10 reads on `/dev/i2c-0`
- local MP3/alarm PCM playback

### `bpi-zero-clock 1.0.3` image-owned

- SPI0 / I2C0 / I2S0 hardware enablement
- `/dev/spidev0.0` binding
- MAX98357A kernel codec module
- MAX98357A Device Tree definition
- PA1 SD/EN codec-driver control
- AP6212 Wi-Fi / Bluetooth kernel and firmware capability

The 2.3.24 application does not remove or modify image-owned hardware capability.

## Not present in mk-clock-adult 2.3.24

- application Bluetooth GUI
- Bluetooth API / IPC
- BlueALSA
- Bluetooth audio routing
- Bluetooth stereo-to-mono matrix
- Bluetooth metadata
- LED control
- Story Mode
