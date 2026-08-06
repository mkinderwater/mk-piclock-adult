#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
core = (root / "mk-piclock.c").read_text(encoding="utf-8")

# Alarm occurrence is durable and checked by local calendar date.
assert "int last_fired_date;" in core
assert "int today = local_date_key(&tmv);" in core
assert "a->last_fired_date != today" in core
assert "a->last_fired_date = today;" in core
assert 'X(i, "last_fired_date", g_state.alarms[i].last_fired_date' in core
assert 'X("alarm_replay_guard_migrated", g_state.alarm_replay_guard_migrated' in core
assert "migrate_alarm_last_fired_dates()" in core
assert "alarm.last_fired_date = previous->last_fired_date;" in core
assert "fsync(fileno(f))" in core
assert "fsync(dir_fd)" in core

check_start = core.index("static void check_alarm(void)")
check_end = core.index("\nint main(void)", check_start)
check_alarm = core[check_start:check_end]
assert check_alarm.index("save_config();") < check_alarm.index("audio_play_music_file(")
assert "fired_yday" not in core

# A second tick changes only cached colon pixels and the seconds row.
assert "struct dashboard_tick_cache" in core
assert "build_dashboard_tick_cache(" in core
assert "update_dashboard_second_tick(" in core
assert "g_dashboard_tick_cache.colon_off_fb[i] !=" in core
assert "draw_dashboard_seconds_position_line(second);" in core
assert "int full_dashboard_redraw = dirty || mode != last_mode ||" in core
assert "tmv.tm_min != last_min;" in core
full_line = core[core.index("int full_dashboard_redraw"):core.index("int second_tick")]
assert "colon_phase" not in full_line

# Dirty rows merge only when their horizontal ranges overlap or touch.
assert "next_min > band_max + 1 || next_max + 1 < band_min" in core
assert "Keep direct partial writes synchronized with dirty comparison" in core

# The dashboard compositor uses dirty flushing rather than an unconditional full transfer.
draw_start = core.index("static void draw_weather_dashboard_screen(void)")
draw_end = core.index("\n\nstruct audio_play_request", draw_start)
dashboard_draw = core[draw_start:draw_end]
assert "(void)oled_flush();" in dashboard_draw
assert "oled_flush_full();" not in dashboard_draw

print("current alarm and OLED guard checks passed")
