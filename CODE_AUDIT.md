# mk-clock-adult 1.2.62 Code Audit

## Scope

Reviewed clock rendering, Weather panel configuration, GeoMet normalization, daily extrema selection, HTTP form parsing, private IPC, OLED rendering, dashboard output, browser controls, installation rules, tests, documentation, and packaged assets.

## Clock alignment

- Removed the fixed clock X correction.
- Added one framebuffer post-render function for both FreeType and built-in clocks.
- The function scans only the clock region, finds visible horizontal bounds, calculates the required translation, clamps it to the panel, and moves the rendered pixels as one image.
- Header drawing occurs after the scan and uses the corrected clock centre axis.

## Today panel

- Added `MP_WEATHER_FRAME_TODAY` to persisted Weather panel configuration.
- Added `MP_WEATHER_SLOT_TODAY` and explicit low, high, availability, and occurrence-hour IPC fields.
- Selects current-day hourly values in the configured Weather timezone.
- Includes the current observed temperature when it expands the available daily range.
- Uses the maximum current-day hourly precipitation probability.
- Added a compact OLED layout without loading an unnecessary Weather icon.
- Anchors TODAY precipitation, OUTSIDE/forecast temperature, and INSIDE humidity to the shared `WEATHER_PANEL_LOWER_ROW_Y` baseline.
- Derives the TODAY low and high rows from a single 15-pixel spacing constant.
- Uses concise low, high, and precipitation rows without occurrence-time text on the OLED.
- Added web controls, dashboard text, API validation, and OpenAPI definitions.

## Weather warning marquee

- Stores the active warning independently from the asynchronously refreshed warning list.
- Applies pending warning changes only after the active display period completes.
- Uses an exact pixel-cycle duration for long warnings and the existing 12-second hold for short warnings.
- Starts scrolling text at the footer's left edge and preserves continuous duplicated-text wraparound.
- Removed the obsolete warning-set start timestamp and its reset path.

## Cleanup and compatibility

- Existing `room`, `outside`, `offset`, and `time` modes remain valid.
- Legacy Weather configuration parsing remains intact.
- The four obsolete room-sensor PNGs and their code paths remain absent.
- Weather scripts remain executable and are also invoked through `sh`.
- Product and web cache keys are now 1.2.62.

## Validation

- Clock visible-bound centring regression tests
- Native AHT10 and font catalogue tests
- Weather configuration and daily low / high tests
- Dashboard daily-panel formatting tests
- INSIDE panel and font-selector tests
- Alarm, diagnostics, backup, restore, and warning-rotation tests
- Native Weather strict build and C test suite
- Browser JavaScript syntax checks
- OpenAPI and JSON validation
- Release-integrity checks

The Native Weather service builds and tests with strict warnings enabled. Full core and API linking is unavailable in this sandbox because the Raspberry Pi audio, GPIO, MP3, and HTTP development headers are not installed.
