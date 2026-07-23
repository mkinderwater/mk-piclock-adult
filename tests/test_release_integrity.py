#!/usr/bin/env python3
from pathlib import Path
import os
import re

project = Path(__file__).resolve().parents[1]
version = "1.2.62"
product = f"mk-clock-adult-{version}"

core = (project / "mk-piclock.c").read_text(encoding="utf-8")
api = (project / "mk-piclock-api.c").read_text(encoding="utf-8")
app = (project / "web/assets/js/app.js").read_text(encoding="utf-8")
index = (project / "web/index.html").read_text(encoding="utf-8")
makefile = (project / "Makefile").read_text(encoding="utf-8")

assert f'#define APP_VERSION "{product}"' in core
assert f'#define PRODUCT_VERSION "{product}"' in api
assert f"const GUI_VERSION = '{product}';" in app
assert "1.2.52" not in index
for match in re.findall(r"[?&]v=([^\"']+)", index):
    assert match == product, f"stale web cache key: {match}"

for script in (project / "weather/install.sh", project / "weather/uninstall.sh"):
    assert os.access(script, os.X_OK), f"script is not executable: {script.relative_to(project)}"
assert "sudo sh ./weather/install.sh --defer-start" in makefile
assert "sudo sh ./weather/uninstall.sh" in makefile
assert "$(MAKE) -C weather clean all" in makefile, "Weather must be rebuilt for the local CPU"


for generated in (
    project / "mk-piclock-core",
    project / "mk-piclock-api",
    project / "tests/test_aht10",
    project / "tests/test_font_catalog",
    project / "weather/build",
):
    assert not generated.exists(), f"generated build artifact packaged: {generated.relative_to(project)}"

for residue in project.rglob("*"):
    if residue.name in {"__pycache__", ".DS_Store"} or residue.suffix in {".pyc", ".pyo", ".o"} or residue.name.endswith("~"):
        raise AssertionError(f"temporary file packaged: {residue.relative_to(project)}")

web = project / "web"
obsolete = (
    "room-sensor-normal.png",
    "room-sensor-waiting.png",
    "room-sensor-stale.png",
    "room-sensor-error.png",
)
for name in obsolete:
    assert not any(web.rglob(name)), f"obsolete web asset packaged: {name}"
for path in web.rglob("*"):
    if not path.is_file():
        continue
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        continue
    for name in obsolete:
        assert name not in text, f"obsolete web reference in {path.relative_to(project)}: {name}"
    for embedded_product in re.findall(r"mk-clock-adult-\d+\.\d+\.\d+", text):
        assert embedded_product == product, (
            f"stale web product/cache version in {path.relative_to(project)}: {embedded_product}"
        )

print("release integrity tests passed")
