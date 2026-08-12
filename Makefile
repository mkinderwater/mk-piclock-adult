CC ?= gcc
PKG_CONFIG ?= pkg-config
CPPFLAGS ?=
CFLAGS ?= -O2
WARNINGS = -Wall -Wextra -Wformat=2 -Werror=unused-function -Werror=implicit-function-declaration
C_STANDARD = -std=gnu11
LDFLAGS ?=
DEFAULT_ALARM_SHA256 := 09c856ce9ef7b4bc9ea258f9b8c822e4ab4695642debfa2a4b3894d98c630fdc
MESSAGE_CHIME_SHA256 := d4962210222af4a36c8cd5aba6998744ff7c1aa080509bf08400ca97cd31d855
I2C_DEVICE ?= /dev/i2c-0
BLUETOOTH_PACKAGES = bluez bluez-alsa-utils
DEBIAN_PACKAGES = gcc make libc6-dev pkg-config ca-certificates tzdata python3 \
	fonts-dejavu-mono \
	libgpiod-dev libfreetype-dev libasound2-dev \
	libmpg123-dev libmicrohttpd-dev libmp3lame-dev libcurl4-openssl-dev \
	libjson-c-dev

CORE_CPPFLAGS = $(CPPFLAGS) -I/usr/include/freetype2
CORE_CFLAGS = $(C_STANDARD) $(WARNINGS) $(CFLAGS)
CORE_LIBS ?= -lgpiod -lfreetype -lasound -lmpg123 -pthread

MHD_CFLAGS := $(shell $(PKG_CONFIG) --cflags libmicrohttpd 2>/dev/null)
MHD_LIBS := $(shell $(PKG_CONFIG) --libs libmicrohttpd 2>/dev/null)
API_CPPFLAGS = $(CPPFLAGS) $(MHD_CFLAGS) -I/usr/include/freetype2
API_CFLAGS = $(C_STANDARD) $(WARNINGS) $(CFLAGS)
API_LIBS ?= $(if $(strip $(MHD_LIBS)),$(MHD_LIBS),-lmicrohttpd) -lfreetype -lmpg123 -lmp3lame -pthread

.PHONY: all build weather clean require-root check-deps check-hardware install uninstall

all: build

build: mk-piclock-core mk-piclock-api weather

mk-piclock-core: mk-piclock.c aht10_sensor.c font_catalog.c util.c hardware_profile.h ipc_protocol.h compiler_attrs.h aht10_sensor.h font_catalog.h util.h
	$(CC) $(CORE_CPPFLAGS) $(CORE_CFLAGS) mk-piclock.c aht10_sensor.c font_catalog.c util.c $(LDFLAGS) $(CORE_LIBS) -lm -o $@

mk-piclock-api: mk-piclock-api.c asset_store.c music_jobs.c font_catalog.c util.c weather_source_store.c weather_frames.c io_helpers.c hardware_profile.h ipc_protocol.h compiler_attrs.h asset_store.h music_jobs.h font_catalog.h util.h weather_source_store.h weather_frames.h weather_version.h io_helpers.h
	$(CC) $(API_CPPFLAGS) $(API_CFLAGS) mk-piclock-api.c weather_source_store.c weather_frames.c io_helpers.c asset_store.c music_jobs.c font_catalog.c util.c $(LDFLAGS) $(API_LIBS) -o $@

weather:
	$(MAKE) -C weather clean all

require-root:
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "ERROR: this target must be run as root."; \
		echo "Log in as root or run: su -"; \
		exit 1; \
	fi

check-deps:
	@missing=""; \
	for pkg in $(DEBIAN_PACKAGES) $(BLUETOOTH_PACKAGES); do \
		dpkg-query -W -f='$${Status}' "$$pkg" 2>/dev/null | grep -q '^install ok installed$$' || missing="$$missing $$pkg"; \
	done; \
	if [ -n "$$missing" ]; then \
		echo "ERROR: required Debian packages are missing:"; \
		for pkg in $$missing; do echo "  $$pkg"; done; \
		echo; \
		echo "Install dependencies manually before running the clock installer:"; \
		echo "  apt-get update"; \
		echo "  apt-get install -y $(DEBIAN_PACKAGES) $(BLUETOOTH_PACKAGES)"; \
		exit 1; \
	fi
	@echo "OK      Required Debian packages are installed"

check-hardware: require-root
	@/bin/bash hardware/verify-bpi-hardware.sh

install: require-root check-deps check-hardware
	@find /usr/share/fonts /usr/local/share/fonts -type f -name DejaVuSansMono.ttf -print -quit 2>/dev/null | grep -q . || { \
		echo "ERROR: DejaVu Sans Mono is missing. Install prerequisite package: fonts-dejavu-mono"; \
		exit 1; \
	}
	@if bluetoothctl show >/dev/null 2>&1; then echo "OK      Bluetooth controller detected"; else echo "WARNING Bluetooth controller is not available yet; clock installation will continue."; fi
	@echo "bpi-zero-clock hardware ready. Building clock application."
	@$(MAKE) --no-print-directory all
	@echo "Installing clock application."
	-systemctl stop mk-piclock-weather.path mk-piclock-weather.timer mk-piclock-weather.service mk-piclock-api.service mk-piclock-core.service mk-clock-bluetooth-control.service mk-clock-bluetooth-audio.service mk-clock-bluealsa.service
	install -m 0644 hardware/mk-piclock.sysusers /usr/lib/sysusers.d/mk-piclock.conf
	systemd-sysusers /usr/lib/sysusers.d/mk-piclock.conf
	@for group in audio spi gpio i2c; do getent group $$group >/dev/null && usermod -a -G $$group mk-piclock-core || true; done
	install -m 0644 hardware/60-mk-piclock-bpi.rules /etc/udev/rules.d/60-mk-piclock-bpi.rules
	install -d -m 0755 /usr/local/lib/mk-piclock /etc/alsa/conf.d
	install -m 0644 bluetooth/asound-mk-piclock.conf /etc/alsa/conf.d/50-mk-piclock.conf
	install -m 0755 bluetooth/mk-clock-bluetooth-control.py /usr/local/lib/mk-piclock/mk-clock-bluetooth-control.py
	install -m 0755 bluetooth/mk-clock-bluealsa-aplay /usr/local/lib/mk-piclock/mk-clock-bluealsa-aplay
	install -m 0644 bluetooth/mk-clock-bluealsa.service /etc/systemd/system/mk-clock-bluealsa.service
	install -m 0644 bluetooth/mk-clock-bluetooth-audio.service /etc/systemd/system/mk-clock-bluetooth-audio.service
	install -m 0644 bluetooth/mk-clock-bluetooth-control.service /etc/systemd/system/mk-clock-bluetooth-control.service
	-udevadm control --reload-rules
	-udevadm trigger --subsystem-match=spidev --action=change
	-udevadm trigger --subsystem-match=gpio --action=change
	-udevadm trigger --subsystem-match=i2c-dev --action=change
	mkdir -p /opt/mk-piclock/assets/music /opt/mk-piclock/assets/music/.processing /opt/mk-piclock/assets/fonts /opt/mk-piclock/config
	rm -rf /opt/mk-piclock/assets/images /opt/mk-piclock/assets/bedtime-images /opt/mk-piclock/assets/stories /opt/mk-piclock/assets/room-sensor
	chown -R mk-piclock-api:mk-piclock /opt/mk-piclock/assets
	chmod -R u=rwX,g=rX,o= /opt/mk-piclock/assets
	chown -R mk-piclock-core:mk-piclock /opt/mk-piclock/config
	chmod -R u=rwX,g=rwX,o= /opt/mk-piclock/config
	install -m 0755 mk-piclock-core /opt/mk-piclock/mk-piclock-core
	install -m 0755 mk-piclock-api /opt/mk-piclock/mk-piclock-api
	@printf '%s  %s\n' "$(DEFAULT_ALARM_SHA256)" assets/default-alarm.mp3 | sha256sum -c -
	install -m 0640 -o root -g mk-piclock assets/default-alarm.mp3 /opt/mk-piclock/assets/default-alarm.mp3
	@printf '%s  %s\n' "$(MESSAGE_CHIME_SHA256)" assets/message-chime.mp3 | sha256sum -c -
	install -m 0640 -o root -g mk-piclock assets/message-chime.mp3 /opt/mk-piclock/assets/message-chime.mp3
	rm -rf /opt/mk-piclock/web /opt/mk-piclock/api
	install -d -m 0755 /opt/mk-piclock/web
	cp -a web/. /opt/mk-piclock/web/
	chown -R root:root /opt/mk-piclock/web
	chmod -R a=rX /opt/mk-piclock/web
	install -m 0644 mk-piclock-core.service /etc/systemd/system/mk-piclock-core.service
	install -m 0644 mk-piclock-api.service /etc/systemd/system/mk-piclock-api.service
	sh ./weather/install.sh --defer-start
	systemctl daemon-reload
	systemctl enable mk-piclock-core.service mk-piclock-api.service mk-piclock-weather.path mk-piclock-weather.timer
	systemctl start mk-piclock-core.service mk-piclock-api.service mk-piclock-weather.path mk-piclock-weather.timer
	systemctl start mk-piclock-weather.service || true
	@if command -v bluetoothctl >/dev/null 2>&1 && command -v bluealsa >/dev/null 2>&1 && command -v bluealsa-aplay >/dev/null 2>&1 && systemctl cat bluetooth.service >/dev/null 2>&1; then \
		systemctl disable --now bluealsa.service bluealsa-aplay.service >/dev/null 2>&1 || true; \
		systemctl enable bluetooth.service mk-clock-bluealsa.service mk-clock-bluetooth-control.service mk-clock-bluetooth-audio.service; \
		systemctl start bluetooth.service mk-clock-bluealsa.service mk-clock-bluetooth-audio.service || true; \
		systemctl restart mk-clock-bluetooth-control.service || true; \
	else echo "INFO    Bluetooth speaker support is not installed; clock services will run normally without it."; fi
	@echo "Installed mk-clock-adult on bpi-zero-clock and started its services."

uninstall: require-root
	-sh ./weather/uninstall.sh
	-systemctl disable --now mk-piclock-api.service mk-piclock-core.service mk-clock-bluealsa.service mk-clock-bluetooth-control.service mk-clock-bluetooth-audio.service
	rm -f /etc/systemd/system/mk-piclock-api.service /etc/systemd/system/mk-piclock-core.service /etc/systemd/system/mk-clock-bluealsa.service /etc/systemd/system/mk-clock-bluetooth-control.service /etc/systemd/system/mk-clock-bluetooth-audio.service
	rm -f /etc/alsa/conf.d/50-mk-piclock.conf /usr/local/lib/mk-piclock/mk-clock-bluetooth-control.py /usr/local/lib/mk-piclock/mk-clock-bluealsa-aplay
	rm -f /etc/udev/rules.d/60-mk-piclock-bpi.rules /usr/lib/sysusers.d/mk-piclock.conf
	-udevadm control --reload-rules
	rm -f /opt/mk-piclock/mk-piclock-api /opt/mk-piclock/mk-piclock-core /opt/mk-piclock/assets/default-alarm.mp3 /opt/mk-piclock/assets/message-chime.mp3
	rm -rf /opt/mk-piclock/web /opt/mk-piclock/api /opt/mk-piclock/assets/room-sensor
	systemctl daemon-reload
	@echo "Removed mk-clock-adult application files. bpi-zero-clock hardware remains installed and unchanged."

clean:
	rm -f mk-piclock-core mk-piclock-api
	$(MAKE) -C weather clean
