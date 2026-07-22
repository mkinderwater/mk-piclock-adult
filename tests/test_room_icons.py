#!/usr/bin/env python3
import struct
from pathlib import Path

project = Path(__file__).resolve().parents[1]
raw_root = project / "assets" / "room-sensor"
web_root = project / "web" / "assets" / "icons"
allowed_levels = {0, 5, 10, 15}
expected_raw = {
    "room-sensor-normal.raw",
    "room-sensor-waiting.raw",
    "room-sensor-stale.raw",
    "room-sensor-error.raw",
}
expected_png = {name.replace(".raw", ".png") for name in expected_raw}

found_raw = {path.name for path in raw_root.glob("*.raw")}
assert expected_raw == found_raw, f"expected {sorted(expected_raw)}, found {sorted(found_raw)}"
assert not list(raw_root.glob("*.png")), "runtime sprite directory must not contain duplicate PNG assets"

for path in sorted(raw_root.glob("*.raw")):
    data = path.read_bytes()
    assert len(data) == 512, f"{path.name}: expected 512 bytes, got {len(data)}"
    for value in data:
        assert value >> 4 in allowed_levels and value & 0x0F in allowed_levels, f"{path.name}: invalid nibble"

found_web = {path.name for path in web_root.glob("room-sensor-*.png")}
assert expected_png == found_web, f"expected web icons {sorted(expected_png)}, found {sorted(found_web)}"
for path in sorted(web_root.glob("room-sensor-*.png")):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{path.name}: invalid PNG signature"
    assert data[12:16] == b"IHDR", f"{path.name}: missing IHDR"
    width, height = struct.unpack(">II", data[16:24])
    assert (width, height) == (32, 32), f"{path.name}: expected 32x32, got {width}x{height}"

print("room sensor icon tests passed")
