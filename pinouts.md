# mk-clock-adult 1.2.40 Pinouts

This document defines the production wiring for the Raspberry Pi 40-pin header, SSD1322 OLED, MAX98357A I2S amplifier, TTP223B touch sensor, AHT10 room sensor, speaker, and external power input.

The software uses **BCM GPIO numbering**. Physical pin numbers refer to the Raspberry Pi 40-pin header. Never identify a connection by wire colour alone.

## Electrical rules

- All logic signals are 3.3 V. Never apply 5 V to a GPIO, SDA, SCL, touch output, or OLED signal pin.
- All modules must share Raspberry Pi ground.
- The OLED, TTP223B, and AHT10 use the Pi 3.3 V rail.
- The MAX98357A uses the Pi 5 V rail but accepts 3.3 V I2S logic.
- The external 5 V header input bypasses the Pi USB power path. Use a regulated supply with adequate current and do not connect USB power at the same time.
- Disconnect power before changing wiring.

## Complete connection table

| Device | Device signal | Raspberry Pi signal | BCM GPIO | Physical pin | Direction |
| --- | --- | --- | ---: | ---: | --- |
| External 5 V supply | +5 V | Pi 5 V input | - | 4 | Supply to Pi |
| External 5 V supply | GND | Pi ground | - | 9 | Supply to Pi |
| SSD1322 OLED | VCC | 3.3 V | - | 1 | Pi to OLED |
| SSD1322 OLED | GND | Ground | - | 6 | Common ground |
| SSD1322 OLED | DIN / MOSI / D1 | SPI0 MOSI | 10 | 19 | Pi to OLED |
| SSD1322 OLED | CLK / SCLK / D0 | SPI0 SCLK | 11 | 23 | Pi to OLED |
| SSD1322 OLED | CS | SPI0 CE0 | 8 | 24 | Pi to OLED |
| SSD1322 OLED | DC / A0 | Data/command | 25 | 22 | Pi to OLED |
| SSD1322 OLED | RST / RES | Reset | 27 | 13 | Pi to OLED |
| MAX98357A amp | VIN | 5 V | - | 2 | Pi to amp |
| MAX98357A amp | GND | Ground | - | 14 | Common ground |
| MAX98357A amp | BCLK | PCM clock | 18 | 12 | Pi to amp |
| MAX98357A amp | LRC / LRCLK / WS | PCM frame sync | 19 | 35 | Pi to amp |
| MAX98357A amp | DIN | PCM data | 21 | 40 | Pi to amp |
| MAX98357A amp | SD / EN | Not connected | - | - | Module default |
| TTP223B touch | VCC | 3.3 V distribution | - | 17 | Pi to sensor |
| TTP223B touch | GND | Ground | - | 39 | Common ground |
| TTP223B touch | OUT / SIG | Digital input | 20 | 38 | Sensor to Pi |
| AHT10 room sensor | VCC | 3.3 V distribution | - | 17 | Pi to sensor |
| AHT10 room sensor | GND | Ground | - | 34 | Common ground |
| AHT10 room sensor | SDA | I2C1 SDA | 2 | 3 | Bidirectional |
| AHT10 room sensor | SCL | I2C1 SCL | 3 | 5 | Bidirectional clock |
| Speaker | `+` | MAX98357A `SPK+` | - | - | Amp to speaker |
| Speaker | `-` | MAX98357A `SPK-` | - | - | Amp to speaker |

The build has three 3.3 V consumers: OLED VCC, TTP223B VCC, and AHT10 VCC. Use a small distribution block, soldered junction, or suitable PCB. Do not force multiple wires into one header contact.

## Raspberry Pi 40-pin header map

```text
                         Raspberry Pi 40-pin header

OLED VCC        <- 3.3 V      ( 1) ( 2)  5 V       -> MAX98357A VIN
AHT10 SDA       <-> GPIO2      ( 3) ( 4)  5 V       <- EXTERNAL +5 V INPUT
AHT10 SCL       <-> GPIO3      ( 5) ( 6)  GND       -> OLED GND
                     unused    ( 7) ( 8)  unused
EXTERNAL GND    ->  GND        ( 9) (10)  unused
                     unused    (11) (12)  GPIO18    -> MAX98357A BCLK
OLED RST        <-  GPIO27     (13) (14)  GND       -> MAX98357A GND
                     unused    (15) (16)  unused
TTP223B + AHT10 <-  3.3 V      (17) (18)  unused
OLED MOSI       <-  GPIO10     (19) (20)  unused
                     unused    (21) (22)  GPIO25    -> OLED DC
OLED SCLK       <-  GPIO11     (23) (24)  GPIO8     -> OLED CS
                     unused    (25) (26)  unused
                     reserved  (27) (28)  reserved
                     unused    (29) (30)  unused
                     unused    (31) (32)  unused
                     unused    (33) (34)  GND       -> AHT10 GND
MAX98357A LRC   <-  GPIO19     (35) (36)  unused
                     unused    (37) (38)  GPIO20    <- TTP223B OUT
TTP223B GND     ->  GND        (39) (40)  GPIO21    -> MAX98357A DIN
```

Pin 1 is the 3.3 V corner pin. Confirm header orientation before applying power. Physical pins 27 and 28 are reserved for HAT identification and are not used by this project.

## Required boot interfaces

The clock requires SPI0 for the OLED, I2C1 for the AHT10, and the MAX98357A I2S overlay for audio. `install.md` is the single authoritative source for all `/boot/firmware/config.txt` or `/boot/config.txt` changes, conflict cleanup, reboot steps, and verification commands.

Do not maintain a second configuration block here. Complete the boot-interface section in `install.md` before powering or testing the attached modules.

## SSD1322 OLED

The core opens `/dev/spidev0.0`, which uses SPI0 CE0.

```text
SSD1322 OLED          Raspberry Pi
-----------------------------------------------
VCC                -> 3.3 V, physical pin 1
GND                -> GND, physical pin 6
DIN / MOSI / D1    -> GPIO10, physical pin 19
CLK / SCLK / D0    -> GPIO11, physical pin 23
CS                 -> GPIO8 CE0, physical pin 24
DC / A0            -> GPIO25, physical pin 22
RST / RES           -> GPIO27, physical pin 13
```

Compiled settings:

```text
SPI device: /dev/spidev0.0
SPI mode:   0
SPI speed:  4 MHz
DC:         BCM GPIO25
RST:        BCM GPIO27
```

Notes:

- The OLED does not use SPI MISO. GPIO9, physical pin 21, remains unused.
- Connect CS to CE0.
- Follow the signal labels printed on the OLED module. Module header order varies.
- Confirm the module is configured for 4-wire SPI and 3.3 V logic.

## AHT10 room temperature and humidity sensor

ROOM reads the AHT10 directly over Linux I2C1. It is independent from ECCC outside weather.

```text
AHT10 module          Raspberry Pi
-----------------------------------------------
VCC                -> 3.3 V distribution from physical pin 17
GND                -> GND, physical pin 34
SDA                -> GPIO2 / SDA1, physical pin 3
SCL                -> GPIO3 / SCL1, physical pin 5
```

Software defaults:

```text
I2C device:   /dev/i2c-1
I2C address:  0x38
Poll period:  10 seconds
Stale limit:  120 seconds
```

Placement and bus notes:

- Power the AHT10 from 3.3 V.
- Keep it away from the Pi CPU, OLED regulator, amplifier, speaker, direct sunlight, and enclosure exhaust. Local heat will bias the reading.
- Leave breathing space around the sensing opening. Do not cover it with adhesive or conformal coating.
- Most breakout boards include SDA and SCL pull-ups. Do not add strong duplicate pull-ups unless required by the module.
- The AHT10 uses fixed address `0x38`. Do not connect a second AHT10 to the same bus.
- The core owns address `0x38`. Do not bind another userspace program or kernel hwmon driver to that address while the clock is running.

ROOM image states:

- Active: thermometer and humidity droplet
- Starting: sensor waiting image
- Stale: retained reading with clock badge
- Unavailable: sensor with X badge

## MAX98357A I2S amplifier

```text
MAX98357A             Raspberry Pi
-----------------------------------------------
VIN                -> 5 V, physical pin 2
GND                -> GND, physical pin 14
BCLK               -> GPIO18, physical pin 12
LRC / LRCLK / WS   -> GPIO19, physical pin 35
DIN                -> GPIO21, physical pin 40
SD / EN            -> Not connected
```

The required audio boot settings are consolidated in `install.md`.

`no-sdmode` means mk-piclock does not control SD or EN from a GPIO. Leave the module SD / EN pad unconnected.

A small click when playback starts is accepted in this release.

## Speaker

```text
MAX98357A SPK+  -> Speaker +
MAX98357A SPK-  -> Speaker -
```

- Do not connect either speaker terminal to Raspberry Pi ground.
- The amplifier output is differential.
- Use one compatible 4 to 8 ohm speaker.

## TTP223B touch sensor

```text
TTP223B               Raspberry Pi
-----------------------------------------------
VCC                -> 3.3 V distribution from physical pin 17
GND                -> GND, physical pin 39
OUT / SIG          -> GPIO20, physical pin 38
```

The core reads GPIO20 as an active-high input with software debounce.

Touch behavior:

- Short touch during audio: stop audio.
- Hold at least 3 seconds and release before 8 seconds: play random music.
- Hold 8 seconds: show network diagnostics.
- Touch again to close diagnostics, or wait for the 30-second timeout.

## GPIOs used by mk-piclock

| BCM GPIO | Physical pin | Interface | Purpose |
| ---: | ---: | --- | --- |
| 2 | 3 | I2C1 | AHT10 SDA |
| 3 | 5 | I2C1 | AHT10 SCL |
| 8 | 24 | SPI0 | OLED CE0 / CS |
| 10 | 19 | SPI0 | OLED MOSI |
| 11 | 23 | SPI0 | OLED clock |
| 18 | 12 | I2S / PCM | MAX98357A BCLK |
| 19 | 35 | I2S / PCM | MAX98357A LRC |
| 20 | 38 | GPIO input | TTP223B OUT |
| 21 | 40 | I2S / PCM | MAX98357A DIN |
| 25 | 22 | GPIO output | OLED DC |
| 27 | 13 | GPIO output | OLED reset |

## Pre-power checklist

- Confirm the external supply is regulated 5 V and sized for the Pi, OLED, amplifier, and speaker load.
- Connect external +5 V to physical pin 4 and external ground to physical pin 9.
- Do not connect USB power while header power is present.
- Confirm pin 1 orientation.
- Confirm every module shares ground.
- Confirm all sensor and OLED logic uses 3.3 V.
- Confirm AHT10 SDA is pin 3 and SCL is pin 5.
- Confirm the amplifier SD / EN pad is unconnected.
- Confirm amplifier DIN is pin 40, not 3.3 V.
- Confirm the speaker connects only to `SPK+` and `SPK-`.
- Check for solder bridges and loose strands.

## Post-boot checklist

```bash
sudo raspi-config nonint get_i2c
ls -l /dev/i2c-1 /dev/spidev0.0
sudo i2cdetect -y 1
```

Expected results:

- `get_i2c` prints `0`, meaning enabled.
- `/dev/i2c-1` and `/dev/spidev0.0` exist.
- `i2cdetect` shows `38`.

Stop `mk-piclock-core.service` before manually scanning the AHT10 bus on an installed clock.
