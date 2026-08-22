# BPI-M2 Zero hardware wiring migration

This note documents the touch-sensor wiring change between older Banana Pi M2 Zero adult-clock releases and the current playback-only hardware contract.

## Touch pin history

| Release generation | TTP223B OUT | PA21 / physical pin 38 | Reason |
|:--|:--|:--|:--|
| Older BPI adult builds through `2.3.50-preview1` | **PA21 / physical pin 38** | Touch input | Original Banana Pi touch wiring |
| Microphone-era previews beginning with `2.3.50-preview2` | **PA17 / physical pin 37** | Reserved for I2S receive data | Touch moved so PA21 could be used by the microphone path |
| Current playback-only builds (`2.3.50-preview33` and later) | **PA17 / physical pin 37** | **Free / unassigned** | Touch stays on PA17 to avoid another physical wiring change |

The current release therefore intentionally does **not** move touch back to pin 38.

## Current TTP223B wiring

```text
TTP223B VCC -> 3.3 V, physical pin 17
TTP223B GND -> GND, physical pin 39
TTP223B OUT -> PA17, physical pin 37, gpiochip0 line 17
```

Physical pin 38 / PA21 must remain disconnected for the current hardware profile.

## Upgrading a clock wired to the older pinout

Power the clock off before changing the header wiring.

1. Leave TTP223B VCC on physical pin 17.
2. Leave TTP223B GND on physical pin 39.
3. Move only the TTP223B `OUT` / `SIG` wire from **physical pin 38** to **physical pin 37**.
4. Leave physical pin 38 / PA21 disconnected.
5. Power the clock on and confirm touch operation.

```text
OLD                                      CURRENT

TTP223B OUT -> pin 38 / PA21             TTP223B OUT -> pin 37 / PA17
pin 37      -> unused                     pin 38      -> free / disconnected
```

If the touch wire is left on physical pin 38 with the current software, touch input will not work because `mk-clock-adult` reads gpiochip0 line 17 / PA17.

## Audio wiring after microphone removal

The MAX98357A playback wiring does not change:

```text
BCLK  -> PA19, physical pin 27
LRC   -> PA18, physical pin 28
DIN   -> PA20, physical pin 40
SD/EN -> PA1,  physical pin 11
```

There is no microphone connection and no I2S receive-data connection in the current build. PA21 / physical pin 38 is free.

## Reference

See `pinouts.md` for the complete current 40-pin header map, OLED, AHT10, MAX98357A, touch, speaker, power, and pre-power checklist.

## Verify the migrated unit

After booting the current build:

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

Play music and press the touch sensor once. The current audio should stop. This confirms the application can see the sensor on PA17 / physical pin 37.
