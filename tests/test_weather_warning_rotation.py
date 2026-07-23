#!/usr/bin/env python3
from pathlib import Path
import re

project = Path(__file__).resolve().parents[1]
weather = (project / 'weather/src/mk-piclock-weather.c').read_text()
core = (project / 'mk-piclock.c').read_text()
api = (project / 'mk-piclock-api.c').read_text()
ipc = (project / 'ipc_protocol.h').read_text()

assert '#define MP_WEATHER_WARNING_SLOTS 8' in ipc
assert '#define MP_WEATHER_WARNING_TEXT_MAX 256' in ipc
assert 'char warning_descriptions[MP_WEATHER_WARNING_SLOTS][MP_WEATHER_WARNING_TEXT_MAX]' in ipc
assert 'active_warning_descriptions' in weather
assert 'warning_display_description' in weather
assert 'warning_rank' not in weather
assert 'for (int i = 0; i < count && used < MP_WEATHER_WARNING_SLOTS; i++)' in weather
assert 'copy_warning_text(out[used], MP_WEATHER_WARNING_TEXT_MAX, description);' in weather

# The OLED owns a stable copy of the active warning. Incoming Weather updates
# replace only the pending list until the active display period completes.
assert 'struct weather_warning_display_state' in core
assert 'static struct weather_warning_display_state g_weather_warning_display' in core
assert 'static int dashboard_weather_warning_state(' in core
assert 'if (elapsed_ms >= duration_ms)' in core
assert 'weather_warning_next_text(' in core
assert 'safe_str(g_weather_warning_display.text,' in core
assert 'g_weather_warning_display.refresh_needed = 1;' in core
assert 'static int dashboard_footer_refresh_active(void)' in core
assert 'warning_set_started_ms' not in core

# Long warnings rotate after one complete pixel cycle, not after a fixed
# minimum that can cut into a second pass.
assert '#define WEATHER_WARNING_MIN_DISPLAY_MS 12000u' in core
assert 'if (width <= available) return WEATHER_WARNING_MIN_DISPLAY_MS;' in core
duration = re.search(
    r'static uint64_t weather_warning_display_duration_ms\(.*?\n\}',
    core,
    re.S,
).group(0)
assert 'return (cycle_pixels * 1000u + DASHBOARD_MARQUEE_SPEED_PX_PER_SEC - 1u) /' in duration
assert 'scroll_ms > WEATHER_WARNING_MIN_DISPLAY_MS' not in duration

# A pass begins at the left edge. The duplicated copy preserves seamless
# wraparound, so resetting at the exact cycle boundary cannot jump mid-text.
assert 'int x = clip_x0 - dashboard_marquee_offset(cycle_width, started_ms);' in core
assert 'x += cycle_width;' in core

# Existing multi-warning API and chime behaviour remain intact.
assert 'new_warning_added' in core
assert 'if (new_warning_added && warning_chime_enabled)' in core
assert r'\"warning_count\":%d,\"warnings\":[' in core
assert 'warning%d_description' in api

print('weather warning rotation checks passed')
