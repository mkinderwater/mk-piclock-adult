# mk-clock-adult 2.3.54 assembly instructions

This manual covers the physical build of the adult clock.

Use `clock-instructions.md` after the clock is assembled and running. Use `INSTALL.md` for software installation. Use `pinouts.md` when you need the complete electrical reference.

## 1. Parts required

Use these parts for the current build:

- Banana Pi M2 Zero board
- USB-C power adapter / breakout for the clock power input
- 3.12-inch SSD1322 256x64 SPI OLED
- AHT10 temperature and humidity sensor
- MAX98357A I2S amplifier
- speaker
- TTP223B capacitive touch sensor
- rubber feet
- regulated 5 V USB-C power supply
- microSD card containing `bpi-zero-clock 1.0.4-preview36`

The speaker is a separate physical part. It connects to the MAX98357A after both parts are mounted.

The software expects the current R1 wiring. Older PA21 touch wiring must be moved to PA17 before use.


## 2. Fastener kit

The enclosure model defines the mounting hardware. Use these sizes unless the physical component has been changed from the production model.

| Location | Qty | Fastener | Why |
| --- | ---: | --- | --- |
| OLED to lid | 4 | M2 x 6 mm countersunk machine screws + M2 nuts | The lid has 2.7 mm M2 clearance slots and a 1.1 mm tapered head recess. This is a through-bolt mount, not a printed thread. |
| Lid to base | 4 | M3 x 10 mm pan-head self-tapping screws | The lid has 3.0 mm clearance holes. The base uses 2.65 mm blind pilots with a 3.05 mm lead-in and 10 mm pilot depth. |
| Banana Pi M2 Zero | 3 | M2.5 x 5 mm pan-head self-tapping screws | Three Pi standoffs use 1.8 mm printed pilots. The fourth Pi mounting hole locates on the printed 2.4 mm anchor peg and does not use a screw. |
| MAX98357A amplifier | 2 | M2.5 x 5 mm pan-head self-tapping screws | Both amplifier standoffs use 1.8 mm printed pilots. |
| Speaker retainers | 2 | M2.5 x 5 mm pan-head self-tapping screws | The two speaker mounting supports use 1.8 mm printed pilots. |
| USB-C adapter | 2 | M2.5 x 5 mm pan-head self-tapping screws | The adapter supports use the same 1.8 mm printed pilot as the Pi hardware. |
| AHT10 | 1 | M3 x 5 mm pan-head self-tapping screw | The model explicitly uses a tapered 2.9 mm to 2.4 mm pilot for an M3 x 5 screw through the sensor board's 3.2 mm mounting hole. |
| TTP223B touch board | 3 | M2.5 x 5 mm pan-head self-tapping screws | The model specifies a 2.5 mm shank, 5 mm screw length and 1.8 mm blind pilots. |
| Rubber feet | 4 | M2.5 x 6 mm pan-head self-tapping screws | Each TPU foot has 2.9 mm shank clearance and a 5.4 mm x 2.0 mm head pocket. The screw then taps into the 3 mm case wall. |

Recommended hardware to have on hand:

- 15 x M2.5 x 5 mm pan-head self-tapping screws
- 4 x M2.5 x 6 mm pan-head self-tapping screws
- 1 x M3 x 5 mm pan-head self-tapping screw
- 4 x M3 x 10 mm pan-head self-tapping screws
- 4 x M2 x 6 mm countersunk machine screws
- 4 x M2 nuts

The M2.5 x 5 total includes 3 Pi, 2 amplifier, 2 speaker, 2 USB-C and 3 touch screws, with 3 spares.

Use screws intended for plastic where specified as self-tapping. Start each screw by hand and stop when the part is seated. The printed pilots are not intended for repeated high-torque removal.

Do not use an 8 mm foot screw by default. The foot screw taps only into the 3 mm case wall and an unnecessarily long screw can project into the enclosure.

## 3. Before assembly

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

## 4. Recommended assembly order

Build the clock in this order:

1. Mount the TTP223B touch sensor using the enclosure foot holes. This must be done first as you will not be able to access the screws to affix.
2. Mount the USB-C adapter.
3. Mount the Banana Pi M2 Zero.
5. Mount the OLED.
6. Mount the speaker.
7. Mount the MAX98357A amplifier.
8. Mount the AHT10 sensor.
10. Fit the rubber feet (10mm sticky back or w/screws).
11. Complete and inspect all wiring.
12. Install the software and run hardware verification.

This order keeps the mechanical work simple and leaves the wiring visible until the final inspection.

## 5. USB-C adapter and Banana Pi

Mount the USB-C adapter in the enclosure so the connector is square with the external opening and can accept a cable without loading the board or wiring. Secure it with **2 x M2.5 x 5 mm pan-head self-tapping screws**.

Mount the Banana Pi M2 Zero on its intended standoffs. The model uses **3 x M2.5 x 5 mm pan-head self-tapping screws**. The fourth mounting hole sits over the printed anchor peg and does not receive a screw. Confirm the 40-pin header remains accessible before tightening the board.

Connect the regulated 5 V output from the USB-C power input to the Banana Pi:

```text
+5 V -> physical pin 4
GND  -> physical pin 6
```

All ground pins on the 40-pin header are common.

Use a USB-C supply with enough current for the Banana Pi, OLED, MAX98357A and speaker load. A weak supply can cause resets, audio problems or unstable Wi-Fi.

The USB-C adapter is the clock's external power input. Do not power the clock from another USB connector or a second 5 V source at the same time.

Do not apply power while the remaining modules are being wired.

## 6. SSD1322 OLED

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

Secure the OLED to the lid with **4 x M2 x 6 mm countersunk machine screws and 4 x M2 nuts**. The horizontal slots allow for PLA/ASA shrink variation. Centre the display before tightening the nuts. Do not use self-tapping screws in the OLED PCB.

The display uses `/dev/spidev0.0`.

OLED MISO is not used. Leave the unused OLED pins disconnected unless the display vendor specifically requires otherwise.

Before continuing, check OLED power polarity twice. The OLED is a 3.3 V device in this build.

## 7. AHT10 temperature and humidity sensor

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

Mount the AHT10 with **1 x M3 x 5 mm pan-head self-tapping screw** through its 3.2 mm board hole into the tapered printed pilot.

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

## 8. MAX98357A amplifier

Mount the MAX98357A with **2 x M2.5 x 5 mm pan-head self-tapping screws** into the printed amplifier standoffs.

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

## 9. Speaker

The speaker is a separate assembly item. Mount it before final wiring so the cone, frame and leads cannot interfere with the Banana Pi, OLED or enclosure.

Position the speaker directly behind the enclosure speaker opening or grille. Seat it flat and retain it with **2 x M2.5 x 5 mm pan-head self-tapping screws** in the two printed speaker supports. Keep adhesive, screws and wiring away from the cone and surround.

Route the two speaker leads back to the MAX98357A. Leave enough slack for assembly, but keep the leads clear of the cone.

Connect the speaker directly to the amplifier:

```text
Speaker + -> MAX98357A SPK+
Speaker - -> MAX98357A SPK-
```

The MAX98357A output is differential. Neither speaker terminal is ground. Do not connect `SPK-` to GND.

Before closing the enclosure, play audio and confirm the speaker is clear, secure and free of buzz or contact with the case.

## 10. TTP223B touch sensor

Connect the touch board:

```text
TTP223B VCC -> 3.3 V, physical pin 17
TTP223B GND -> GND, physical pin 39
TTP223B OUT -> PA17, physical pin 37
```

Physical pin 38 / PA21 is not used by this release.

Older clock builds used pin 38 for touch. Move that wire to pin 37 before installing this release.

### Touch placement

Use **3 x M2.5 x 5 mm pan-head self-tapping screws** to secure the TTP223B. The two right-side screws are driven through the screwdriver access holes hidden below the right rubber feet. Secure the sensor before fitting the rubber feet.

Keep the sensing face close to the intended outer touch surface. Thick plastic, a large air gap or metal between the sensor and the user's finger can reduce sensitivity.

Confirm the sensor cannot move when the clock is handled. Test touch operation before fitting the feet permanently.

The finished clock uses touch for:

- stopping active audio
- starting random Music with a 3-second hold
- starting a random Podcast with 10 short taps in 8 seconds, when enabled
- opening diagnostics with a 15-second hold

## 11. Rubber feet

Fit the rubber feet after the touch sensor is secured and its wiring has been tested. Secure each foot with **1 x M2.5 x 6 mm pan-head self-tapping screw**. The foot provides 2.9 mm shank clearance and a recessed 5.4 mm head pocket.

The foot holes are part of the touch-sensor mounting arrangement. Confirm the touch board is held correctly before the feet are fully seated.

Check that all feet sit flat so the clock does not rock and that no foot or mounting fastener pinches the touch-sensor wiring.

## 12. Complete 40-pin connections

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

## 13. Enclosure layout

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

## 14. Pre-power inspection

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

## 15. First power-on

Insert the prepared microSD card and apply the regulated 5 V supply.

The board should boot without repeated resets. The OLED should initialize once the clock services start.

If the board repeatedly reboots, disconnect power and check the 5 V supply and wiring before continuing.

## 16. Install the clock software

The base image must be `bpi-zero-clock 1.0.4-preview36` with the current playback hardware contract.

Extract the mk-clock-adult release on the clock and run:

```bash
./install.sh
```

The release ships its install and hardware verification scripts as executable files. A normal release extraction should not require `chmod`.

The installer verifies the expected hardware baseline before installing the clock services.

See `INSTALL.md` for the complete software procedure.

## 17. Verify the hardware after install

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

## 18. Functional test

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

## 19. Close the enclosure

Close the case only after OLED, sensor, audio, touch and network tests pass.

Close the enclosure with **4 x M3 x 10 mm pan-head self-tapping screws** through the lid into the blind corner pilots.

Before installing the final screws:

- confirm no wire is pinched
- confirm the speaker cone remains free
- confirm the AHT10 ventilation remains open
- confirm the touch board has not shifted
- confirm the microSD card cannot fall out

Power the completed clock once more after closing the enclosure and repeat the basic OLED, sensor, audio and touch checks.

## 20. Service reference

For normal operation, read `clock-instructions.md`.

For software installation or upgrades, read `INSTALL.md`.

For exact pin assignments, read `pinouts.md`.

For older PA21 touch builds, read `HARDWARE_MIGRATION.md`.
