# BPI-M2 Zero touch migration

Current mk-clock-adult releases use **PA17 / physical pin 37** for TTP223B OUT.

Older BPI adult-clock builds used **PA21 / physical pin 38**.

The current release keeps touch on pin 37. Pin 38 is free.

## Current touch wiring

```text
TTP223B VCC -> 3.3 V, physical pin 17
TTP223B GND -> GND, physical pin 39
TTP223B OUT -> PA17, physical pin 37, gpiochip0 line 17
```

Leave physical pin 38 / PA21 disconnected.

## Migrate an older clock

Power the clock off.

1. Leave TTP223B VCC on pin 17.
2. Leave TTP223B GND on pin 39.
3. Move TTP223B OUT/SIG from pin 38 to pin 37.
4. Leave pin 38 disconnected.
5. Power the clock on.
6. Test touch.

```text
OLD                         CURRENT
OUT -> pin 38 / PA21        OUT -> pin 37 / PA17
pin 37 unused               pin 38 free
```

If OUT remains on pin 38, touch will not work because the application reads gpiochip0 line 17 / PA17.

## Audio wiring

MAX98357A wiring is unchanged:

```text
BCLK  -> PA19, physical pin 27
LRC   -> PA18, physical pin 28
DIN   -> PA20, physical pin 40
SD/EN -> PA1,  physical pin 11
```

There is no microphone or I2S receive-data connection in this release.

## Verify

After boot:

```bash
grep -E '^(TOUCH_GPIO|TOUCH_HEADER_PIN|I2S_RX_GPIO|I2S_RX_HEADER_PIN)=' \
  /etc/bpi-zero-clock-release
```

Expected:

```text
TOUCH_GPIO=PA17
TOUCH_HEADER_PIN=37
I2S_RX_GPIO=unassigned
I2S_RX_HEADER_PIN=38-free
```

Play audio and press touch once. Playback should stop.

See `pinouts.md` for complete wiring.
