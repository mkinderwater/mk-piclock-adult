# Weather Dashboard Layout

Native OLED size: `256 x 64`

## Horizontal allocation

The display uses a 40/20/20/20 allocation:

```text
Location, clock and date | Weather panel 1 | Weather panel 2 | Weather panel 3
x = 0..102               | x = 103..153    | x = 154..204    | x = 205..255
width = 103              | width = 51      | width = 51      | width = 51
```

Vertical separators occupy `x = 102, 153, 204`, from `y = 3..60`.

Each Weather panel may independently be INSIDE, OUTSIDE, TODAY, hours ahead, or a specific forecast time.

## Clock region

```text
Weather location: x = 2..100, y = 3..7
Clock time:       x = 2..100, y = 7..48
Seconds line:     x = 5..97,  y = 50
Clock date:       x = 2..100, y = 54..60
```

The selected clock font is rendered first. The framebuffer is then scanned within the clock region, and the rendered clock is translated until its actual illuminated left and right bounds are centred. This removes font-dependent offsets caused by advance width, side bearings, or narrow digits.

The Weather location and optional `- ALARM ON` suffix use the same corrected clock centre axis.

## Weather panel labels

Every panel label uses the fixed 5 x 7 renderer at `y = 3..9`.

- INSIDE panels show `INSIDE`.
- OUTSIDE panels show `OUTSIDE`.
- TODAY panels show `TODAY`.
- Forecast panels show the selected hour, such as `7AM` or `6PM`.

## INSIDE panel

```text
INSIDE label:          y = 3..9
Large TTF temperature: y = 13..43
Relative humidity:     y = 47..51
```

The temperature uses the selected INSIDE font and is formatted with one decimal place, for example `23.1°`. An empty INSIDE font selection follows the clock font. Relative humidity is formatted as `45%` and shares the `y = 47` baseline used by OUTSIDE and forecast temperatures.

## OUTSIDE and forecast panels

```text
Weather icon:          32 x 32, centred at y = 28
Temperature/precip:    y = 47..51
Future forecast date:  y = 56..60
```

The footer is formatted as `25^ (20%)` when precipitation is non-zero and `25^` otherwise. Missing temperatures render as `--^`.

## TODAY high / low panel

The TODAY panel does not draw an icon. It uses three evenly spaced compact data rows. Labels share a fixed left edge and values share a fixed right edge. Its precipitation row shares the same lower-row baseline as OUTSIDE temperature and INSIDE humidity:

```text
TODAY label: y = 3..9
Low row:     y = 17..21   label: L     right-aligned value: 10^
High row:    y = 32..36   label: H     right-aligned value: 24^
Precip row:  y = 47..51   label: POP   right-aligned value: 40%
```

Occurrence hours remain in Weather status data but are intentionally omitted from the OLED panel.

The low and high are selected from available ECCC hourly entries on the current local date, with the current observed temperature also considered. `POP` is the highest available hourly precipitation probability for the current date.

## Specific-time selection

A fixed-time panel targets the next occurrence of the configured hour in `MK_WEATHER_TIMEZONE`. Once that hour has passed, the target advances to the following day. The panel then uses the first ECCC hourly forecast at or after that target.
