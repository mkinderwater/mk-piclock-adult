#!/usr/bin/env python3
import json
from pathlib import Path

project = Path(__file__).resolve().parents[1]
frames_h = (project / "weather_frames.h").read_text(encoding="utf-8")
frames_c = (project / "weather_frames.c").read_text(encoding="utf-8")
weather_c = (project / "weather" / "src" / "mk-piclock-weather.c").read_text(encoding="utf-8")
api_c = (project / "mk-piclock-api.c").read_text(encoding="utf-8")
core_c = (project / "mk-piclock.c").read_text(encoding="utf-8")
ipc_h = (project / "ipc_protocol.h").read_text(encoding="utf-8")
html = (project / "web" / "modules" / "weather" / "module.html").read_text(encoding="utf-8")
js = (project / "web" / "modules" / "weather" / "module.js").read_text(encoding="utf-8")
sample = (project / "weather" / "config" / "weather-frames.conf").read_text(encoding="utf-8")
openapi = json.loads((project / "api" / "openapi-v1.json").read_text(encoding="utf-8"))

assert "MP_WEATHER_FRAME_TIME = 4" in frames_h
assert "MP_WEATHER_FRAME_TODAY = 5" in frames_h
assert "int time_hour;" in frames_h
assert 'case MP_WEATHER_FRAME_TIME: return "time";' in frames_c
assert 'case MP_WEATHER_FRAME_TODAY: return "today";' in frames_c
assert "specific-time weather panels require time_hour" in frames_c
assert "selection->mode != MP_WEATHER_FRAME_TIME" in weather_c
assert "local_frame_target(now, selection, timezone_name)" in weather_c

for slot in (1, 2, 3):
    assert f'id="weather-frame-{slot}-time"' in html
    assert f'name="slot{slot}_time"' in html
    assert f"`slot${{slot}}_time`" in js
    assert f'\\"slot{slot}\\":{{\\"mode\\":\\"%s\\",\\"offset_hours\\":%d,\\"time\\":\\"%02d:00\\"}}' in api_c
    assert f"slot{slot}_time_hour=" in sample

assert html.count('<option value="time">Specific time</option>') == 3
assert html.count('<option value="today">Today low / high</option>') == 3
assert "['room', 'outside', 'today', 'offset', 'time']" in js
assert "specific weather-panel time must be on the hour" in api_c
assert "build_today_slot" in weather_c
assert "MP_WEATHER_SLOT_TODAY" in weather_c
assert "MP_WEATHER_SLOT_TODAY = 4" in ipc_h
assert "static void draw_weather_today_panel(" in core_c
assert "static void draw_weather_compact_text_right_aligned(" in core_c
assert "const int label_x = x0 + 5;" in core_c
assert "const int value_x1 = x1 - 5;" in core_c
assert 'draw_weather_compact_text(label_x, WEATHER_TODAY_LOW_ROW_Y, "L", 13);' in core_c
assert 'draw_weather_compact_text(label_x, WEATHER_TODAY_HIGH_ROW_Y, "H", 13);' in core_c
assert 'draw_weather_compact_text(label_x, WEATHER_PANEL_LOWER_ROW_Y, "POP", 11);' in core_c
assert '"%d^"' in core_c
assert 'snprintf(precipitation_value, sizeof(precipitation_value), "%d%%"' in core_c
assert "format_weather_summary_hour" not in core_c
assert '"L %d^ %s"' not in core_c
assert '"H %d^ %s"' not in core_c
assert "draw_weather_today_panel(x0, x1, &weather.slots[i]);" in core_c

schema = openapi["paths"]["/api/v1/config/weather-frames"]
for slot in (1, 2, 3):
    get_slot = schema["get"]["responses"]["200"]["content"]["application/json"]["schema"]["properties"][f"slot{slot}"]
    assert "time" in get_slot["properties"]
    assert "time" in get_slot["required"]
    assert "time" in get_slot["properties"]["mode"]["enum"]
    assert "today" in get_slot["properties"]["mode"]["enum"]

    post_schema = schema["post"]["requestBody"]["content"]["application/x-www-form-urlencoded"]["schema"]
    assert f"slot{slot}_time" in post_schema["properties"]
    assert f"slot{slot}_time" in post_schema["required"]
    assert "time" in post_schema["properties"][f"slot{slot}_mode"]["enum"]
    assert "today" in post_schema["properties"][f"slot{slot}_mode"]["enum"]

print("weather panel configuration tests passed")
