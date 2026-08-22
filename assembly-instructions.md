# mk-clock-adult 2.3.54 assembly instructions

This manual covers the physical build of the adult clock.

Use `clock-instructions.md` after the clock is assembled and running. Use `INSTALL.md` for software installation. Use `pinouts.md` when you need the complete electrical reference.

## 1. Hardware used

The current release targets:

- Banana Pi M2 Zero
- 3.12-inch SSD1322 256x64 SPI OLED
- MAX98357A I2S amplifier
- speaker connected to the MAX98357A differential output
- AHT10 temperature and humidity sensor
- TTP223B capacitive touch sensor
- regulated 5 V power supply
- microSD card containing `bpi-zero-clock 1.0.4-preview36`

The software expects the current R1 wiring. Older PA21 touch wiring must be moved to PA17 before use.

## 2. Before assembly

Confirm the Banana Pi header orientation before connecting anything.

Physical pin 1 is the 3.3 V pin used by the OLED. Do not use cable colour as the only reference. Confirm the board markings and pin numbers.

Keep these rules in mind:

- use one regulated 5 V power source
- connect all module grounds together
- power the OLED from 3.3 V
- power the MAX98357A from 5 V
- power the AHT10 and TTP223B from 3.3 V
- connect the speaker only between `SPK+` and `SPK-`
- leave physical pin 38 disconnected
- connect TTP223B OUT to physical pin 37

Do not power the finished clock from USB and the 5 V header at the same time.

## 3. Recommended assembly order

Build and test in this order:

1. Banana Pi and power
2. OLED
3. AHT10 sensor
4. MAX98357A amplifier and speaker
5. TTP223B touch sensor
6. enclosure mounting
7. software installation and hardware verification

This makes faults easier to isolate. Test each subsystem before permanently closing the enclosure.

## 4. Banana Pi power

Connect the external regulated supply:

```text
+5 V -> physical pin 4
GND  -> physical pin 6
```

All ground pins on the 40-pin header are common.

Use a supply with enough current for the Banana Pi, OLED, amplifier and speaker load. A weak supply can produce random resets, audio problems or unstable Wi-Fi.

Do not apply power yet if the other modules are still being wired.

## 5. SSD1322 OLED

Connect the OLED as follows:

```text
OLED VSS      -> GND, physical pin 6
OLED VCC_IN   -> 3.3 V, physical pin 1
OLED D0/CLK   -> PC2 / SPI0 SCLK, physical pin 23
OLED D1/DIN   -> PC0 / SPI0 MOSI, physical pin 19
OLED D/C#     -> PA2, physical pin 22
OLED RES#     -> PA0, physical pin 13
OLED CS#      -> PC3 / SPI0 CS0, physical pin 24
```

The display uses `/dev/spidev0.0`.

OLED MISO is not used. Leave the unused OLED pins disconnected unless the display vendor specifically requires otherwise.

Before continuing, check OLED power polarity twice. The OLED is a 3.3 V device in this build.

## 6. AHT10 temperature and humidity sensor

Connect the AHT10:

```text
AHT10 VCC -> 3.3 V, physical pin 17
AHT10 GND -> GND, physical pin 9
AHT10 SDA -> PA12, physical pin 3
AHT10 SCL -> PA11, physical pin 5
```

The software expects:

```text
I2C device: /dev/i2c-0
Address:    0x38
```

### Sensor placement

Mount the AHT10 where room air can reach it.

Keep it away from:

- the Banana Pi CPU
- the MAX98357A amplifier
- the speaker magnet
- direct airflow from a vent
- direct sunlight
- the 5 V regulator or other heat-producing parts

The sensor measures the air around the clock. If it is trapped beside a warm board, the indoor temperature will read high.

Do not seal the sensor inside an airtight enclosure. Provide ventilation openings near it.

## 7. MAX98357A amplifier

Connect the amplifier:

```text
MAX98357A VIN   -> 5 V, physical pin 2
MAX98357A GND   -> GND, physical pin 14
MAX98357A BCLK  -> PA19, physical pin 27
MAX98357A LRC   -> PA18, physical pin 28
MAX98357A DIN   -> PA20, physical pin 40
MAX98357A SD/EN -> PA1, physical pin 11
```

The codec driver controls SD/EN. The application does not drive that line directly.

The current audio configuration uses:

```text
mclk-fs = 256
SD/EN delay = 5 ms
```

## 8. Speaker

Connect the speaker directly to the MAX98357A:

```text
Speaker + -> SPK+
Speaker - -> SPK-
```

The amplifier output is differential. Neither speaker terminal is ground.

Do not connect `SPK-` to GND.

Mount the speaker so the cone has clearance to move. Avoid pressing the cone against the enclosure or wiring. Use an opening or grille so sound is not trapped inside the case.

## 9. TTP223B touch sensor

Connect the touch board:

```text
TTP223B VCC -> 3.3 V, physical pin 17
TTP223B GND -> GND, physical pin 39
TTP223B OUT -> PA17, physical pin 37
```

Physical pin 38 / PA21 is not used by this release.

Older clock builds used pin 38 for touch. Move that wire to pin 37 before installing this release.

### Touch placement

Mount the touch sensor directly behind the intended touch area of the enclosure.

Keep the sensing face close to the outer surface. Thick plastic, large air gaps or metal between the sensor and the user's finger can reduce sensitivity.

Test the touch sensor before permanently bonding it in place.

The finished clock uses touch for:

- stopping active audio
- starting random Music with a 3-second hold
- starting a random Podcast with 10 short taps in 8 seconds, when enabled
- opening diagnostics with a 15-second hold

## 10. Complete 40-pin connections

Use this table as the assembly checklist.

| Physical pin | Connection | Signal |
| ---: | --- | --- |
| 1 | OLED VCC_IN | 3.3 V |
| 2 | MAX98357A VIN | 5 V |
| 3 | AHT10 SDA | PA12 |
| 4 | Main power input | 5 V |
| 5 | AHT10 SCL | PA11 |
| 6 | Main/OLED ground | GND |
| 9 | AHT10 ground | GND |
| 11 | MAX98357A SD/EN | PA1 |
| 13 | OLED RES# | PA0 |
| 14 | MAX98357A ground | GND |
| 17 | AHT10 + TTP223B VCC | 3.3 V |
| 19 | OLED D1/DIN | PC0 / SPI0 MOSI |
| 22 | OLED D/C# | PA2 |
| 23 | OLED D0/CLK | PC2 / SPI0 CLK |
| 24 | OLED CS# | PC3 / SPI0 CS0 |
| 27 | MAX98357A BCLK | PA19 |
| 28 | MAX98357A LRC | PA18 |
| 37 | TTP223B OUT | PA17 |
| 38 | Free | PA21 |
| 39 | TTP223B ground | GND |
| 40 | MAX98357A DIN | PA20 |

## 11. Enclosure layout

Arrange the components so each part can do its job without interfering with another.

Recommended priorities:

- OLED centred and square behind the display opening
- touch sensor directly behind the intended touch surface
- speaker facing a grille/opening
- AHT10 near ventilation and away from internal heat
- Banana Pi positioned so the microSD card remains serviceable if practical
- wiring kept clear of the speaker cone
- power wiring kept short and secure
- no exposed conductor able to contact the enclosure or another header pin

Provide enough strain relief that moving the clock or opening the case does not pull directly on header connections.

## 12. Pre-power inspection

Before applying power, verify each item:

- physical pin 1 orientation is correct
- +5 V enters physical pin 4
- power ground enters physical pin 6
- OLED VCC is on 3.3 V, not 5 V
- OLED CLK is on pin 23
- OLED MOSI is on pin 19
- OLED D/C is on pin 22
- OLED reset is on pin 13
- OLED CS is on pin 24
- AHT10 SDA is on pin 3
- AHT10 SCL is on pin 5
- AHT10 is powered from 3.3 V
- MAX98357A BCLK is on pin 27
- MAX98357A LRC is on pin 28
- MAX98357A DIN is on pin 40
- MAX98357A SD/EN is on pin 11
- speaker is connected only to SPK+ and SPK-
- TTP223B OUT is on pin 37
- physical pin 38 is disconnected
- all grounds are common
- no loose strands or solder bridges are present

If any connection is uncertain, correct it before applying power.

## 13. First power-on

Insert the prepared microSD card and apply the regulated 5 V supply.

The board should boot without repeated resets. The OLED should initialize once the clock services start.

If the board repeatedly reboots, disconnect power and check the 5 V supply and wiring before continuing.

## 14. Install the clock software

The base image must be `bpi-zero-clock 1.0.4-preview36` with the current playback hardware contract.

Extract the mk-clock-adult release on the clock and run:

```bash
./install.sh
```

The release ships its install and hardware verification scripts as executable files. A normal release extraction should not require `chmod`.

The installer verifies the expected hardware baseline before installing the clock services.

See `INSTALL.md` for the complete software procedure.

## 15. Verify the hardware after install

Check the base-image hardware metadata:

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

The audio card should include `MAX98357A`. The I2S device should expose playback without a capture endpoint.

## 16. Functional test

Test the finished clock before closing the enclosure.

### OLED

Confirm:

- time is visible
- display is stable
- no random corruption or flicker appears

### AHT10

Open the web GUI and confirm indoor temperature and RH are present.

The temperature trend arrow needs sensor history before it can appear. No arrow immediately after boot is normal.

### Audio

Play a Music or Podcast file from the GUI.

Confirm:

- audio is clean
- volume responds
- speaker does not buzz against the enclosure

### Touch

While audio is playing, touch the sensor once. Playback should stop.

Then test the configured hold/tap shortcuts.

### Network

Open the clock web GUI from another device. Confirm the expected hostname/IP and weather status.

## 17. Close the enclosure

Close the case only after OLED, sensor, audio, touch and network tests pass.

Before installing the final screws:

- confirm no wire is pinched
- confirm the speaker cone remains free
- confirm the AHT10 ventilation remains open
- confirm the touch board has not shifted
- confirm the microSD card cannot fall out

Power the completed clock once more after closing the enclosure and repeat the basic OLED, sensor, audio and touch checks.

## 18. Service reference

For normal operation, read `clock-instructions.md`.

For software installation or upgrades, read `INSTALL.md`.

For exact pin assignments, read `pinouts.md`.

For older PA21 touch builds, read `HARDWARE_MIGRATION.md`.
