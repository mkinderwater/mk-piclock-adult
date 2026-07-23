#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "mk-piclock.c").read_text(encoding="utf-8")
start = source.index("static int weather_compact_glyph")
end = source.index("static void draw_weather_compact_text", start)
helpers = source[start:end]

harness = f'''#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WEATHER_LEFT_X0 0
#define WEATHER_LEFT_X1 102

static void mp_safe_str(char *dst, size_t dst_size, const char *src) {{
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    size_t length = strlen(src);
    if (length >= dst_size) length = dst_size - 1;
    memcpy(dst, src, length);
    dst[length] = '\\0';
}}
#define safe_str mp_safe_str

{helpers}

int main(void) {{
    char label[96];

    make_weather_header_label("Invermere", 0, label, sizeof(label));
    assert(strcmp(label, "INVERMERE") == 0);

    make_weather_header_label("Invermere", 1, label, sizeof(label));
    assert(strcmp(label, "INVERMERE - ALARM ON") == 0);
    assert(weather_compact_text_width(label) <= WEATHER_LEFT_X1 - WEATHER_LEFT_X0 - 4);
    assert(weather_compact_text_optical_x(label, 47) == 12);

    make_weather_header_label(NULL, 1, label, sizeof(label));
    assert(strcmp(label, "ALARM ON") == 0);

    make_weather_header_label("Radium Hot Springs", 1, label, sizeof(label));
    assert(strstr(label, " - ALARM ON") != NULL);
    assert(weather_compact_text_width(label) <= WEATHER_LEFT_X1 - WEATHER_LEFT_X0 - 4);

    make_weather_header_label("Fort Nelson, British Columbia", 1, label, sizeof(label));
    assert(strncmp(label, "FORT NELSON", strlen("FORT NELSON")) == 0);
    assert(strstr(label, " - ALARM ON") != NULL);

    puts("alarm header tests passed");
    return 0;
}}
'''

with tempfile.TemporaryDirectory() as tmp:
    c_file = Path(tmp) / "test_alarm_header.c"
    binary = Path(tmp) / "test_alarm_header"
    c_file.write_text(harness, encoding="utf-8")
    subprocess.run([
        "gcc", "-std=gnu11", "-Wall", "-Wextra", "-Wformat=2",
        "-Wformat-truncation=2", "-Werror", "-O2",
        str(c_file), "-o", str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True)
