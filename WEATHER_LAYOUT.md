# Weather Dashboard Layout

Native OLED size: `256 x 64`

## Horizontal allocation

The display uses a 40/20/20/20 allocation. Integer pixel widths are chosen so all 256 columns are used:

```text
Location, clock and date | AHT10 ROOM | Next forecast | Later forecast
x = 0..102     | x = 103..153 | x = 154..204 | x = 205..255
width = 103    | width = 51    | width = 51    | width = 51
```

Vertical separators:

```text
x = 102, 153, 204
y = 3..60
OLED level = 7
```

The separators are exactly 51 pixels apart throughout the weather area.

## Clock region

```text
Weather location: x = 2..100, y = 1..5
Clock time:       x = 2..100, y = 7..48
Clock date:       x = 2..100, y = 56..62
```

The Weather location comes from the configured ECCC source, is converted to uppercase, and is centred with the compact 3x5 font. The main clock is fitted from a maximum of 72 pixels down to the largest size that keeps the worst-case `88:88` footprint inside its region. Its vertical centre is Y=27.5, matching the Weather-icon centre at Y=28. FreeType coverage is thresholded at 128, so each clock pixel is either level 0 or level 15.

The clock-region date uses the core's internal fixed 5x7 font.

## Vertical layout

```text
Weather location:      y = 1..5, compact 3x5 text
Forecast labels:       y = 3..9
Weather icons:         32 x 32, centred at y = 28, occupying y = 12..43
Forecast values:       y = 46..50, compact 3x5 text
Future forecast date:  y = 55..59, compact 3x5 text
Clock time region:     y = 7..48
Clock-region date:     y = 56..62, fixed 5x7 text
```

Each outdoor forecast footer is formatted as `25^ (20%)` when the selected hour has a non-zero precipitation chance. With a zero or unavailable chance, it is formatted as `25^`. ROOM is formatted as `21^ 42%`, using AHT10 temperature and relative humidity. The compact 3x5 footer keeps each string within its 51-pixel panel.

When a selected forecast falls after the current date in the configured Weather timezone, a second compact line appears at Y=55..59, for example `JULY 23`. Same-day panels leave this line blank. Full month names fit within the 51-pixel panel, including `SEPTEMBER 30`.

The first panel is labelled `ROOM` and uses the local AHT10 sensor. It always draws a dedicated thermometer-and-droplet sprite rather than a weather condition. Active, starting, stale, and unavailable states use separate 32 x 32 sprites. Outdoor weather is never substituted into ROOM.

Configurable panels set to `Outside` show the realtime ECCC observation under the heading `OUTSIDE`. When ECCC provides no realtime temperature or icon, only OUTSIDE uses the nearest hourly value for the missing display field; daily highs and lows are never substituted.

The separate precipitation and UV metrics panel is not drawn. Top-level values remain available through the API.


## Clock vertical balance

The clock time uses Y=7..48, placing its centre at Y=27.5. The Weather icons are centred at Y=28, so the clock and icons appear level. The location occupies Y=1..5 above the clock, and the date occupies Y=56..62 at the bottom of the panel.
