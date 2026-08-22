# mk-clock-adult 2.3.54 pinouts

Hardware: Banana Pi M2 Zero, 40-pin CON2 header.

Base image: `bpi-zero-clock 1.0.4-preview36`.

Kernel: `6.12.101+deb13-armmp`.

The application uses `/dev/gpiochip0` line offsets, not Raspberry Pi BCM numbers.

## Important touch change

Current touch OUT is **PA17 / physical pin 37**.

Older builds used **PA21 / physical pin 38**.

Leave pin 38 disconnected. See `HARDWARE_MIGRATION.md` before upgrading older wiring.

## Complete wiring

| Physical pin | Device / signal | BPI signal | gpiochip0 line | Use |
|--:|:--|:--|--:|:--|
| 1 | SSD1322 VCC_IN | 3.3 V | - | OLED power |
| 2 | MAX98357A VIN | 5 V | - | Amplifier power |
| 3 | AHT10 SDA | PA12 / TWI0 SDA | 12 | I2C |
| 4 | Clock power input | 5 V | - | External regulated +5 V |
| 5 | AHT10 SCL | PA11 / TWI0 SCL | 11 | I2C |
| 6 | Clock / OLED ground | GND | - | Ground |
| 9 | AHT10 ground | GND | - | Ground |
| 11 | MAX98357A SD / EN | PA1 | 1 | Codec-controlled enable |
| 13 | SSD1322 RES# | PA0 | 0 | OLED reset |
| 14 | MAX98357A GND | GND | - | Ground |
| 17 | AHT10 + TTP223B VCC | 3.3 V | - | Shared 3.3 V |
| 19 | SSD1322 D1 / DIN | PC0 / SPI0 MOSI | 64 | OLED data |
| 22 | SSD1322 D/C# | PA2 | 2 | OLED D/C |
| 23 | SSD1322 D0 / CLK | PC2 / SPI0 CLK | 66 | OLED clock |
| 24 | SSD1322 CS# | PC3 / SPI0 CS0 | 67 | OLED chip select |
| 27 | MAX98357A BCLK | PA19 / I2S0 BCLK | 19 | I2S |
| 28 | MAX98357A LRC | PA18 / I2S0 LRCLK | 18 | I2S |
| 37 | TTP223B OUT | PA17 | 17 | Touch input |
| 38 | **FREE** | PA21 | 21 | Unassigned |
| 39 | TTP223B GND | GND | - | Ground |
| 40 | MAX98357A DIN | PA20 / I2S0 DOUT | 20 | I2S |
| - | Speaker + | MAX98357A SPK+ | - | Speaker |
| - | Speaker - | MAX98357A SPK- | - | Speaker |

## Header map

```text
                              BPI-M2 Zero CON2

OLED VCC       <- 3.3 V       (1)  (2)  5 V          -> MAX98357A VIN
AHT10 SDA      <- PA12        (3)  (4)  5 V          <- EXTERNAL +5 V INPUT
AHT10 SCL      <- PA11        (5)  (6)  GND          <- CLOCK/OLED GND
                              (7)  (8)
AHT10 GND      <- GND         (9) (10)
MAX98357A EN   <- PA1        (11) (12)
OLED RST       <- PA0        (13) (14) GND           -> MAX98357A GND
                             (15) (16)
AHT10/TOUCH VCC<- 3.3 V      (17) (18)
OLED MOSI      <- PC0        (19) (20) GND
OLED MISO unused, PC1        (21) (22) PA2           -> OLED DC
OLED SCLK      <- PC2        (23) (24) PC3           -> OLED CS
GND                          (25) (26)
MAX98357A BCLK <- PA19       (27) (28) PA18          -> MAX98357A LRC
                             (29) (30) GND
                             (31) (32)
                             (33) (34) GND
                             (35) (36)
TTP223B OUT    <- PA17       (37) (38) PA21          -> FREE
TTP223B GND    <- GND        (39) (40) PA20          -> MAX98357A DIN
```

## SSD1322 OLED

3.12-inch, 256x64, 4-wire SPI:

```text
VSS      -> GND, pin 6
VCC_IN   -> 3.3 V, pin 1
D0/CLK   -> PC2 / SPI0 SCLK, pin 23
D1/DIN   -> PC0 / SPI0 MOSI, pin 19
D/C#     -> PA2, pin 22
RES#     -> PA0, pin 13
CS#      -> PC3 / SPI0 CS0, pin 24
```

OLED pins 3 and 6 through 13 remain disconnected.

The core uses `/dev/spidev0.0`. MISO / physical pin 21 is unused.

## AHT10

```text
VCC -> 3.3 V, pin 17
GND -> GND, pin 9
SDA -> PA12, pin 3
SCL -> PA11, pin 5
```

Software device:

```text
/dev/i2c-0
address 0x38
```

## MAX98357A

```text
VIN   -> 5 V, pin 2
GND   -> GND, pin 14
BCLK  -> PA19, pin 27
LRC   -> PA18, pin 28
DIN   -> PA20, pin 40
SD/EN -> PA1, pin 11
```

The codec driver controls PA1. The application does not drive it directly.

`mclk-fs = 256`.

SD/EN delay is 5 ms.

Connect the speaker only between `SPK+` and `SPK-`. The output is differential. Do not ground either speaker terminal.

## TTP223B

```text
VCC -> 3.3 V, pin 17
GND -> GND, pin 39
OUT -> PA17, pin 37, gpiochip0 line 17
```

Touch actions:

```text
Short press during audio -> stop audio
Hold 3 seconds, release  -> play random uploaded music
Hold 15 seconds          -> network diagnostics
10 short taps / 8 sec    -> random podcast, when enabled
```

Physical pin 38 / PA21 is free.

## Power

Use a regulated 5 V supply sized for the Banana Pi, OLED and amplifier.

```text
+5 V -> physical pin 4
GND  -> physical pin 6
```

All header ground pins are common.

Use one power source. Do not power by USB and header at the same time.

## Pre-power check

- Confirm pin 1 orientation.
- Confirm +5 V on pin 4 and GND on pin 6.
- Confirm all modules share ground.
- Confirm OLED power polarity.
- Confirm OLED CLK/MOSI/D-C/RST/CS on pins 23/19/22/13/24.
- Confirm AHT10 SDA/SCL on pins 3/5.
- Confirm MAX98357A BCLK/LRC/DIN on pins 27/28/40.
- Confirm MAX98357A SD/EN on pin 11.
- Confirm touch OUT on pin 37.
- Confirm pin 38 is disconnected.
- Confirm speaker is connected only to `SPK+` and `SPK-`.

## Verify after install

Check base-image metadata:

```bash
grep -E '^(VERSION|KERNEL_ABI|AUDIO_MODE|AUDIO_CAPTURE|I2S_RX_GPIO|I2S_RX_HEADER_PIN|TOUCH_GPIO|TOUCH_HEADER_PIN)=' \
  /etc/bpi-zero-clock-release
```

Expected touch values:

```text
I2S_RX_GPIO=unassigned
I2S_RX_HEADER_PIN=38-free
TOUCH_GPIO=PA17
TOUCH_HEADER_PIN=37
```

Check ALSA:

```bash
cat /proc/asound/cards
cat /proc/asound/pcm
```

The card should include `MAX98357A`. I2S should expose playback and no capture endpoint.

Play audio and press touch once. Playback should stop.
