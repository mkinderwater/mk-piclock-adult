#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "mk-piclock.c").read_text(encoding="utf-8")

assert "DASHBOARD_CLOCK_X_SHIFT" not in source
assert "#define DASHBOARD_CLOCK_CONTENT_X0 (WEATHER_LEFT_X0 + 2)" in source
assert "#define DASHBOARD_CLOCK_CONTENT_X1 (WEATHER_LEFT_X1 - 2)" in source
assert "static int dashboard_center_rendered_clock_x(" in source
assert "if (oled_get_px(x, y) == 0) continue;" in source
assert "const int target_center2 = x0 + x1;" in source
assert "const int ink_center2 = ink_x0 + ink_x1;" in source
assert "shift_x = clamp_int(shift_x, x0 - ink_x0, x1 - ink_x1);" in source
assert "oled_set_px(destination_x, y, row[i]);" in source

render_start = source.index("static void draw_weather_dashboard_screen(void)")
render = source[render_start:]
clock_call = render.index("dashboard_center_rendered_clock_x(")
header_call = render.index("weather_compact_text_optical_x(")
fallback_call = render.index("draw_dashboard_time_pixel_fallback(")
assert fallback_call < clock_call < header_call

# The function centres the visible bounds, independent of the nominal font grid.
def centred_shift(x0: int, x1: int, ink_x0: int, ink_x1: int) -> int:
    delta2 = (x0 + x1) - (ink_x0 + ink_x1)
    shift = (delta2 + 1) // 2 if delta2 >= 0 else -((-delta2 + 1) // 2)
    return max(x0 - ink_x0, min(shift, x1 - ink_x1))

for ink_x0, ink_x1 in ((2, 90), (10, 100), (22, 78), (36, 63), (3, 99)):
    shift = centred_shift(2, 100, ink_x0, ink_x1)
    moved_x0 = ink_x0 + shift
    moved_x1 = ink_x1 + shift
    assert moved_x0 >= 2 and moved_x1 <= 100
    assert abs((moved_x0 + moved_x1) - (2 + 100)) <= 1

# Forecast panels, separators, and seconds remain tied to fixed panel geometry.
assert "static const int weather_panel_x0[MP_WEATHER_FORECAST_SLOTS] = {103, 154, 205};" in source
assert "static const int weather_separators[] = {102, 153, 204};" in source
assert "const int line_x0 = usable_x0 + DASHBOARD_SECONDS_LINE_CLEAR_PX;" in source

print("clock alignment tests passed")
