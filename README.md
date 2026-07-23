# mk-clock-adult 1.2.62

Native Raspberry Pi alarm clock software for the SSD1322 OLED, TTP223B touch input, MAX98357A I2S audio, ECCC Weather, and an AHT10 inside temperature and humidity sensor.

## What changed

- Aligns all TODAY values to one right-hand pixel column while keeping `L`, `H`, and `POP` on a fixed left edge.
- Simplifies INSIDE humidity from `37% RH` to `37%`.
- Retains the shared lower-row baseline across TODAY, INSIDE, OUTSIDE, and forecast panels.
- Retains font-independent clock centring, Today low / high data, the INSIDE font selector, and the corrected Weather installer.

## Today low / high data

The daily panel evaluates ECCC hourly entries that fall on the current date in `MK_WEATHER_TIMEZONE`. The current observed temperature is also considered. Precipitation is the highest hourly probability available for the current day.

If ECCC does not provide an earlier hourly value, an earlier low or high that already occurred cannot be reconstructed. The panel reports the best current-day data available to the service.

## Compatibility

```text
Product:     mk-clock-adult-1.2.62
HTTP API:    1.44
Private IPC: 27
Weather:     Native C 2.0.14
Inside sensor: Native AHT10
```

Existing Weather panel configuration remains compatible. The new stored panel mode is `today`. The INSIDE source mode remains `room` for compatibility.

## Build and install

See `install.md` for dependencies, boot configuration, wiring, build, installation, verification, and troubleshooting.

## Hardware

See `pinouts.md` for the complete Raspberry Pi GPIO and peripheral wiring map.
