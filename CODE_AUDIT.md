# mk-clock-adult 1.2.40 Code Audit

The code audit is unchanged from the validated 1.2.39 audit. Release 1.2.40 changes installation documentation and version identifiers only.

## Scope

Reviewed the native core, HTTP API, Weather daemon, shared configuration code, browser JavaScript, tests, installation rules, and packaged assets.

## Removed or consolidated

- Removed unused exported function `mp_system_font_find_filename()`.
- Consolidated duplicate weather-panel configuration parsing into `weather_frames.c`.
- Consolidated duplicate full-write and parent-directory sync logic into `io_helpers.c`.
- Consolidated duplicate 32 x 32 grayscale icon loading and rendering in the core.
- Consolidated weather slot kind names and safe default labels in `ipc_protocol.h`.
- Removed duplicate ROOM PNG masters, obsolete preview PNGs, and the contact sheet from `assets/room-sensor`.

## Logic corrections

- Added per-panel temperature availability across Weather, API, IPC, core, status JSON, OLED, and GUI.
- Missing temperatures now display as `--°`; valid `0°C` readings remain valid.
- Removed fabricated startup weather readings and weather icons.
- Changed omitted availability fields to unavailable instead of assuming valid zero values.
- Changed label fallbacks from panel position assumptions to source-aware labels: ROOM, OUTSIDE, or LATER.
- Fixed unchecked `/proc/net/route` header reading.
- Fixed a malformed shared-config `snprintf` call and added strict format warnings plus a serialization test.
- Updated the Weather user agent to follow the compiled Weather version.
- Weather activity records now state when temperature is unavailable instead of logging `0 C`.

## Configuration rules

The shared weather-panel parser now rejects:

- Unknown keys
- Duplicate keys
- Invalid modes
- Offsets outside 1 through 48 hours
- Incomplete configurations
- Mixed legacy and current fields
- Overlong lines

Legacy 1.2.37 migration remains covered by tests.

## Validation completed

- Native Weather build with `-Wall -Wextra -Wformat=2 -Werror`
- Native Weather regression suite
- AHT10 decoder tests
- Dashboard Weather JavaScript tests
- ROOM RAW and browser PNG validation
- Clang syntax checks for all native source files using hardware-library stubs
- Clang static analysis for all native source files
- AddressSanitizer and UndefinedBehaviorSanitizer runs for Weather and AHT10 tests
- Exact duplicate function-body scan
- Exported-symbol usage scan
- Duplicate packaged-file hash scan
- OpenAPI JSON and JavaScript syntax validation

No known dead functions, exact duplicate function bodies, duplicate packaged files, or unreferenced ROOM runtime assets remain.
