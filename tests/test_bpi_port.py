#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
profile = (root / "hardware_profile.h").read_text(encoding="utf-8")
core = (root / "mk-piclock.c").read_text(encoding="utf-8")
api = (root / "mk-piclock-api.c").read_text(encoding="utf-8")
service = (root / "mk-piclock-core.service").read_text(encoding="utf-8")
rules = (root / "hardware/60-mk-piclock-bpi.rules").read_text(encoding="utf-8")
dts = (root / "hardware/max98357a-bpi-m2-zero.dts").read_text(encoding="utf-8")

expected_profile = {
    'MP_PLATFORM_PROFILE': '"BPI-M2 Zero"',
    'MP_OLED_SPI_DEV': '"/dev/spidev0.0"',
    'MP_GPIO_CHIP': '"/dev/gpiochip0"',
    'MP_GPIO_OLED_RST': '0',
    'MP_GPIO_OLED_DC': '2',
    'MP_GPIO_TOUCH': '21',
    'MP_AHT10_I2C_DEVICE': '"/dev/i2c-0"',
    'MP_ALSA_CARD_MATCH': '"MAX98357A"',
    'MP_AUDIO_FORCE_STEREO': '1',
}
for name, value in expected_profile.items():
    assert f"#define {name} {value}" in profile, f"missing {name}={value}"

assert "MPG123_FORCE_STEREO" in core
assert "snd_card_next" in core
assert "MP_ALSA_CARD_MATCH" in core
assert "platform_profile" in api
assert "board_serial" in api
assert 'Environment=MK_AHT10_DEVICE=/dev/i2c-0' in service
assert 'SupplementaryGroups=audio spi gpio i2c' in service
assert 'KERNEL=="i2c-0"' in rules
for token in ('simple-audio-card', 'PA18', 'PA19', 'PA20', 'mclk-fs = <256>'):
    assert token in dts, f"missing BPI audio overlay token: {token}"

active_files = [
    root / "hardware_profile.h",
    root / "mk-piclock.c",
    root / "mk-piclock-api.c",
    root / "mk-piclock-core.service",
    root / "Makefile",
    root / "install.md",
    root / "pinouts.md",
]
active_files.extend((root / "web").rglob("*.js"))
for path in active_files:
    text = path.read_text(encoding="utf-8")
    assert "/dev/i2c-1" not in text, f"stale RPi I2C device in {path.relative_to(root)}"

print("BPI platform regression checks passed")
