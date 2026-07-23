#!/usr/bin/env python3
from pathlib import Path
import json
root = Path(__file__).resolve().parents[1]
api = (root / "mk-piclock-api.c").read_text(encoding="utf-8")
ui = (root / "web/modules/system/module.js").read_text(encoding="utf-8")
spec = json.loads((root / "api/openapi-v1.json").read_text(encoding="utf-8"))
for token in ["hardware_model", "inventory_id", "storage_used_bytes", "sd_capacity_bytes", "room_sensor_status", "weather_source_url", "MP_WEATHER_VERSION"]:
    assert token in api, token
assert "/api/v1/diagnostics/report" in api
assert "diag-storage-used" in ui and "diag-room-status" in ui
assert "/api/v1/diagnostics/report" in spec["paths"]
assert 'snprintf(out, out_len, "/dev/%s", name)' not in api
assert 'snprintf(out, out_len, "MK-%s", compact)' not in api
assert 'if (out_len <= 5 || name_len > out_len - 6) return -1;' in api
assert 'if (copy_len > out_len - 4) copy_len = out_len - 4;' in api
print("expanded diagnostics checks passed")
