# mk-clock-adult 2.3.54-preview59 BPI-M2 Zero pinouts

This document covers the Banana Pi M2 Zero 40-pin CON2 header used by the adult clock with `bpi-zero-clock 1.0.4-preview36`. Physical pin 38 / PA21 is unassigned and free. Touch is physical pin 37 / PA17.

Paired kernel ABI: `6.12.101+deb13-armmp`.

The application uses `/dev/gpiochip0` line offsets, not Raspberry Pi BCM numbers.

## Touch wiring revision

The current touch mapping differs from older Banana Pi adult-clock wiring:

| Wiring generation | TTP223B OUT | Physical pin 38 / PA21 |
|:--|:--|:--|
| Older builds through `2.3.50-preview1` | **PA21 / pin 38** | Touch input |
| Microphone-era previews beginning with `2.3.50-preview2` | **PA17 / pin 37** | Reserved for I2S receive data |
| Current playback-only build | **PA17 / pin 37** | **Free / unassigned** |

Touch deliberately stays on PA17 / physical pin 37 to avoid requiring another hardware rewiring change. **Do not move the touch wire back to pin 38.**

To upgrade an older unit, power it off and move only TTP223B `OUT` / `SIG` from physical pin 38 to physical pin 37. Leave TTP223B VCC on pin 17 and GND on pin 39. Leave pin 38 disconnected. See `HARDWARE_MIGRATION.md` for the complete migration note.

## Complete wiring

| Physical pin | Device / signal | BPI signal | gpiochip0 line | Direction / use |
|--:|:--|:--|--:|:--|
| 1 | SSD1322 VCC_IN | 3.3 V | - | Board to OLED |
| 2 | MAX98357A VIN | 5 V | - | Board to amplifier |
| 3 | AHT10 SDA | PA12 / TWI0 SDA | 12, I2C-owned | Bidirectional |
| 4 | Clock power input | 5 V | - | Regulated external +5 V |
| 5 | AHT10 SCL | PA11 / TWI0 SCL | 11, I2C-owned | Board to sensor |
| 6 | Clock / OLED ground | GND | - | Common ground |
| 9 | AHT10 ground | GND | - | Common ground |
| 11 | MAX98357A SD / EN | PA1 | 1, codec-owned | Board to amplifier |
| 13 | SSD1322 RES# | PA0 | 0 | Board to OLED |
| 14 | MAX98357A GND | GND | - | Common ground |
| 17 | AHT10 + TTP223B VCC | 3.3 V | - | Shared 3.3 V branch |
| 19 | SSD1322 D1 / DIN | PC0 / SPI0 MOSI | 64, SPI-owned | Board to OLED |
| 22 | SSD1322 D/C# | PA2 | 2 | Board to OLED |
| 23 | SSD1322 D0 / CLK | PC2 / SPI0 CLK | 66, SPI-owned | Board to OLED |
| 24 | SSD1322 CS# | PC3 / SPI0 CS0 | 67, SPI-owned | Board to OLED |
| 27 | MAX98357A BCLK | PA19 / I2S0 BCLK | 19, I2S-owned | Board to amplifier |
| 28 | MAX98357A LRC / LRCLK / WS | PA18 / I2S0 LRCLK | 18, I2S-owned | Board to amplifier |
| 37 | TTP223B OUT | PA17 | 17 | Sensor to board |
| 38 | **FREE** | PA21 | 21 | Unassigned |
| 39 | TTP223B GND | GND | - | Common ground |
| 40 | MAX98357A DIN | PA20 / I2S0 DOUT | 20, I2S-owned | Board to amplifier |
| - | Speaker + | MAX98357A SPK+ | - | Amplifier to speaker |
| - | Speaker - | MAX98357A SPK- | - | Amplifier to speaker |

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

Confirmed 3.12-inch 256x64 SSD1322 module, 4-wire SPI:

```text
OLED pin 1  VSS      -> GND, physical pin 6
OLED pin 2  VCC_IN   -> 3.3 V, physical pin 1
OLED pin 4  D0/CLK   -> PC2 / SPI0 SCLK, physical pin 23
OLED pin 5  D1/DIN   -> PC0 / SPI0 MOSI, physical pin 19
OLED pin 14 D/C#     -> PA2, physical pin 22
OLED pin 15 RES#     -> PA0, physical pin 13
OLED pin 16 CS#      -> PC3 / SPI0 CS0, physical pin 24
```

OLED pins 3 and 6 through 13 are disconnected. The core opens `/dev/spidev0.0`; MISO/pin 21 is unused by the OLED.

## AHT10

```text
VCC -> 3.3 V, physical pin 17
GND -> GND, physical pin 9
SDA -> PA12 / TWI0 SDA, physical pin 3
SCL -> PA11 / TWI0 SCL, physical pin 5
```

Software device: `/dev/i2c-0`, address `0x38`.

## MAX98357A amplifier

```text
VIN   -> 5 V, physical pin 2
GND   -> GND, physical pin 14
BCLK  -> PA19, physical pin 27
LRC   -> PA18, physical pin 28
DIN   -> PA20, physical pin 40
SD/EN -> PA1, physical pin 11
```

The playback-only image owns I2S0 and PA1. The codec driver enables the amplifier after clock stabilization. `mclk-fs = 256`; SD/EN delay is 5 ms. The application does not drive PA1 directly.

The speaker output is differential. Connect the speaker only between `SPK+` and `SPK-`; neither speaker terminal goes to ground.

## TTP223B touch sensor

```text
VCC -> 3.3 V, physical pin 17
GND -> GND, physical pin 39
OUT -> PA17, physical pin 37, gpiochip0 line 17
```

```text
Short press during audio: stop current audio
Hold 3 seconds, release:  play random uploaded music
Hold 15 seconds:           show network diagnostics
```

Physical pin 38 / PA21 is intentionally free. Older releases used this pin for touch; the current application reads PA17 / pin 37 instead.

## Power

Use a regulated 5 V supply suitable for the Banana Pi, OLED and amplifier load. Header power input is physical pin 4. Clock ground is physical pin 6. All header ground pins are common. Do not connect USB power at the same time as header power.

## Pre-power checklist

- Confirm CON2 pin 1 orientation.
- Confirm external +5 V is on physical pin 4 and ground on pin 6.
- Confirm all modules share ground.
- Confirm OLED pin 1 is ground and pin 2 is 3.3 V.
- Confirm OLED CLK/MOSI/D-C/RST/CS are physical pins 23/19/22/13/24.
- Confirm AHT10 SDA/SCL are physical pins 3/5.
- Confirm MAX98357A BCLK/LRC/DIN are physical pins 27/28/40.
- Confirm MAX98357A SD/EN is physical pin 11 / PA1.
- Confirm touch OUT is physical pin 37 / PA17.
- If upgrading older wiring, confirm TTP223B OUT was moved from physical pin 38 to pin 37.
- Confirm physical pin 38 / PA21 is not connected.
- Confirm the speaker is connected only to `SPK+` and `SPK-`.
- After install/reboot, confirm `/dev/spidev0.0`, `/dev/i2c-0`, `/dev/gpiochip0` and MAX98357A playback.

## Post-install hardware verification

Confirm the paired base-image hardware metadata:

```bash
grep -E '^(VERSION|KERNEL_ABI|AUDIO_MODE|AUDIO_CAPTURE|I2S_RX_GPIO|I2S_RX_HEADER_PIN|TOUCH_GPIO|TOUCH_HEADER_PIN)=' \
    /etc/bpi-zero-clock-release
```

Expected touch/free-pin values:

```text
I2S_RX_GPIO=unassigned
I2S_RX_HEADER_PIN=38-free
TOUCH_GPIO=PA17
TOUCH_HEADER_PIN=37
```

Confirm playback exists and no capture PCM is exposed:

```bash
cat /proc/asound/cards
cat /proc/asound/pcm
```

The ALSA card should include `MAX98357A`; the I2S PCM should expose playback and no capture endpoint.

Finally test the physical touch sensor. A short press while audio is playing must stop the current audio. If touch does not respond after upgrading an older unit, power off and verify that TTP223B `OUT` is on physical pin 37 rather than the legacy physical pin 38.
