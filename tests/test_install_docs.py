#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
install = (root / "install.md").read_text(encoding="utf-8")
pinouts = (root / "pinouts.md").read_text(encoding="utf-8")

assert "Banana Pi M2 Zero" in install
assert "Armbian_community_26.8.0-trunk.413_Bananapim2zero_trixie_current_6.18.38_minimal" in install
assert "/boot/armbianEnv.txt" in install
assert "overlays=spi-spidev i2c0" in install
assert "spi0 spi-spidev" not in install
assert "user_overlays=max98357a-bpi-m2-zero" in install
assert "hardware/max98357a-bpi-m2-zero.dts" in install
assert "armbian-add-overlay" in install
assert "/dev/spidev0.0" in install
assert "/dev/i2c-0" in install
assert "cat /proc/asound/cards" in install
assert "sudo reboot" in install
assert "config.txt" not in install
assert "Raspberry Pi" not in pinouts
assert "dtoverlay=" not in pinouts

print("BPI installation documentation checks passed")

assert "SharpExt4Explorer" in install
assert "e2fsprogs" in install
assert '/root/.not_logged_in_yet' in install
assert 'hidden: true' in install
assert 'Normal/broadcast SSID example' in install
assert 'PRESET_NET_WIFI_SSID="MJ - IoT"' in install
