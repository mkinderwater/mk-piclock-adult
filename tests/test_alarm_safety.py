#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
core = (root / "mk-piclock.c").read_text(encoding="utf-8")
dashboard = (root / "web/modules/dashboard/module.js").read_text(encoding="utf-8")
system = (root / "web/modules/system/module.js").read_text(encoding="utf-8")

assert "#define ALARM_MAX_DURATION_SECONDS 1800" in core
assert "mpg123_seek(mh, 0, SEEK_SET)" in core
assert "last_successful_alarm" in core
assert "next_alarm_time" in core and "format_next_alarm" in core
assert 'next_alarm_text' in core
assert "ntp-warning" in dashboard
assert "system-ntp-warning" in system
assert 'static const char alarm_suffix[] = " - ALARM ON";' in core
assert "g_state.alarms[i].enabled && g_state.alarms[i].weekdays" in core
assert "make_weather_header_label(weather.location, alarm_on" in core
assert '<strong>Audio</strong><span id="summary-sound">' in (root / "web/modules/dashboard/module.html").read_text(encoding="utf-8")
assert ": 'Stopped'));" in dashboard
assert "'Quiet'" not in dashboard
print("alarm safety and status checks passed")
