#!/usr/bin/env python3
from pathlib import Path

project = Path(__file__).resolve().parents[1]
core = (project / "mk-piclock.c").read_text(encoding="utf-8")
makefile = (project / "Makefile").read_text(encoding="utf-8")
ipc = (project / "ipc_protocol.h").read_text(encoding="utf-8")
api = (project / "mk-piclock-api.c").read_text(encoding="utf-8")
html = (project / "web" / "modules" / "display" / "module.html").read_text(encoding="utf-8")
js = (project / "web" / "modules" / "display" / "module.js").read_text(encoding="utf-8")

assert not (project / "assets" / "room-sensor").exists(), \
    "OLED INSIDE sprite assets must be removed"
assert "ROOM_SENSOR_ASSET_DIR" not in core
assert "draw_room_sensor_icon" not in core
assert "draw_room_temperature_ttf_binary" in core
assert "draw_room_temperature_panel" in core
assert 'snprintf(temperature_text, sizeof(temperature_text), "%.1f", temperature);' in core
assert 'snprintf(humidity_text, sizeof(humidity_text), "%d%%", humidity);' in core
assert 'safe_str(humidity_text, sizeof(humidity_text), "--%");' in core
assert '"%d%% RH"' not in core
assert '#define WEATHER_PANEL_LOWER_ROW_Y 47' in core
assert 'WEATHER_PANEL_LOWER_ROW_Y,\n        humidity_text, humidity_level' in core
assert 'draw_weather_compact_text(label_x, WEATHER_PANEL_LOWER_ROW_Y, "POP", 11);' in core
assert 'value_x1, WEATHER_PANEL_LOWER_ROW_Y, precipitation_value, 11' in core
assert 'WEATHER_PANEL_LOWER_ROW_Y,\n                weather.slots[i].temperature_c' in core
assert '#define WEATHER_TODAY_ROW_SPACING 15' in core
assert '(WEATHER_PANEL_LOWER_ROW_Y - WEATHER_TODAY_ROW_SPACING)' in core
assert '(WEATHER_TODAY_HIGH_ROW_Y - WEATHER_TODAY_ROW_SPACING)' in core
assert 'safe_str(label, sizeof(label), "INSIDE");' in core
assert 'inside_font_file[0] ? inside_font_file : font_file' in core
assert 'MP_IPC_DISPLAY_INSIDE_FONT_FILE' in ipc
assert 'char inside_font_file[128];' in ipc
assert 'form_value(context, "inside_font_file")' in api
assert 'id="inside-font"' in html and 'name="inside_font_file"' in html
assert "setInsideFontChoice" in js and "Same as clock font" in js
assert "assets/room-sensor/*.raw" not in makefile

# The obsolete web sensor-state PNGs and their image-only UI path must stay removed.
web_root = project / "web"
for name in (
    "room-sensor-normal.png",
    "room-sensor-waiting.png",
    "room-sensor-stale.png",
    "room-sensor-error.png",
):
    assert not any(web_root.rglob(name)), f"obsolete web asset returned: {name}"
for path in web_root.rglob("*"):
    if not path.is_file():
        continue
    try:
        content = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        continue
    assert "room-sensor-icon" not in content, f"obsolete room sensor image UI in {path}"
    assert "room-sensor-normal.png" not in content, f"obsolete asset reference in {path}"
    assert "room-sensor-waiting.png" not in content, f"obsolete asset reference in {path}"
    assert "room-sensor-stale.png" not in content, f"obsolete asset reference in {path}"
    assert "room-sensor-error.png" not in content, f"obsolete asset reference in {path}"

print("room temperature panel tests passed")
