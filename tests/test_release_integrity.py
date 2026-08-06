#!/usr/bin/env python3
from pathlib import Path
import os
import re

project = Path(__file__).resolve().parents[1]
product = "mk-clock-adult-1.2.65-bpi-m2-zero-r1"

core = (project / "mk-piclock.c").read_text(encoding="utf-8")
api = (project / "mk-piclock-api.c").read_text(encoding="utf-8")
app = (project / "web/assets/js/app.js").read_text(encoding="utf-8")
index = (project / "web/index.html").read_text(encoding="utf-8")
makefile = (project / "Makefile").read_text(encoding="utf-8")
profile = (project / "hardware_profile.h").read_text(encoding="utf-8")

assert f'#define MP_PRODUCT_VERSION "{product}"' in profile
assert '#define APP_VERSION MP_PRODUCT_VERSION' in core
assert '#define PRODUCT_VERSION MP_PRODUCT_VERSION' in api
assert f"const GUI_VERSION = '{product}';" in app
assert "const $$" not in app and "        $$," not in app
for match in re.findall(r"[?&]v=([^\"']+)", index):
    assert match == product, f"stale web cache key: {match}"

for script in (project / "weather/install.sh", project / "weather/uninstall.sh"):
    assert os.access(script, os.X_OK), f"script is not executable: {script.relative_to(project)}"
assert "sudo sh ./weather/install.sh --defer-start" in makefile
assert "sudo sh ./weather/uninstall.sh" in makefile
assert "$(MAKE) -C weather clean all" in makefile
assert "hardware/60-mk-piclock-bpi.rules" in makefile
assert "hardware/mk-piclock.sysusers" in makefile
assert "hardware/max98357a-bpi-m2-zero.dts" in (project / "install.md").read_text(encoding="utf-8")

for generated in (
    project / "mk-piclock-core", project / "mk-piclock-api",
    project / "tests/test_aht10", project / "tests/test_font_catalog",
    project / "weather/build",
):
    assert not generated.exists(), f"generated build artifact packaged: {generated.relative_to(project)}"

for residue in project.rglob("*"):
    if residue.name in {"__pycache__", ".DS_Store"} or residue.suffix in {".pyc", ".pyo", ".o"} or residue.name.endswith("~"):
        raise AssertionError(f"temporary file packaged: {residue.relative_to(project)}")

for path in (project / "web").rglob("*"):
    if not path.is_file(): continue
    try: text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError: continue
    for embedded in re.findall(r"mk-clock-adult-\d+\.\d+\.\d+(?:-bpi-m2-zero-r\d+)?", text):
        assert embedded == product, f"stale web product/cache version in {path.relative_to(project)}: {embedded}"

print("release integrity tests passed")
