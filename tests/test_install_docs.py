#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
install = (root / "install.md").read_text(encoding="utf-8")
pinouts = (root / "pinouts.md").read_text(encoding="utf-8")

required_block = """[all]
dtparam=spi=on
dtparam=i2c_arm=on
dtparam=audio=off
dtoverlay=max98357a,no-sdmode"""

ini_blocks = re.findall(r"```ini\n(.*?)\n```", install, flags=re.DOTALL)
assert required_block in ini_blocks, "authoritative config.txt block is missing or changed"
assert sum(block == required_block for block in ini_blocks) == 1, "authoritative config.txt block must appear once"
assert "## Configure Raspberry Pi boot interfaces" in install
assert "/boot/firmware/config.txt" in install
assert "/boot/config.txt" in install
assert "sudo reboot" in install
assert "/dev/spidev0.0" in install
assert "/dev/i2c-1" in install
assert "cat /proc/asound/cards" in install
assert "dtparam=" not in pinouts, "pinouts.md must not duplicate config.txt settings"
assert "dtoverlay=" not in pinouts, "pinouts.md must not duplicate config.txt overlays"

print("install documentation checks passed")
