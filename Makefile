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

CORE_CPPFLAGS = $(CPPFLAGS) -I/usr/include/freetype2
CORE_CFLAGS = $(C_STANDARD) $(WARNINGS) $(CFLAGS)
CORE_LIBS ?= -lgpiod -lfreetype -lasound -lmpg123 -pthread

MHD_CFLAGS := $(shell $(PKG_CONFIG) --cflags libmicrohttpd 2>/dev/null)
MHD_LIBS := $(shell $(PKG_CONFIG) --libs libmicrohttpd 2>/dev/null)
API_CPPFLAGS = $(CPPFLAGS) $(MHD_CFLAGS) -I/usr/include/freetype2
API_CFLAGS = $(C_STANDARD) $(WARNINGS) $(CFLAGS)
API_LIBS ?= $(if $(strip $(MHD_LIBS)),$(MHD_LIBS),-lmicrohttpd) -lfreetype -lmpg123 -lmp3lame -pthread

.PHONY: all build prepare-build weather test clean check-hardware install uninstall

all: prepare-build
	@$(MAKE) --no-print-directory build

build: mk-piclock-core mk-piclock-api weather

prepare-build:
	@future_files="$$(find . -type f ! -path './.git/*' ! -path './weather/build/*' ! -name 'mk-piclock-core' ! -name 'mk-piclock-api' -newermt 'now + 2 seconds' -print 2>/dev/null)"; 	if [ -n "$$future_files" ]; then 		echo "Normalizing future-dated files before build..."; 		printf '%s\n' "$$future_files" | while IFS= read -r file; do touch "$$file"; done; 		rm -f mk-piclock-core mk-piclock-api; 		$(MAKE) --no-print-directory -C weather clean; 	fi

mk-piclock-core: mk-piclock.c aht10_sensor.c font_catalog.c util.c hardware_profile.h ipc_protocol.h compiler_attrs.h aht10_sensor.h font_catalog.h util.h
	$(CC) $(CORE_CPPFLAGS) $(CORE_CFLAGS) mk-piclock.c aht10_sensor.c font_catalog.c util.c $(LDFLAGS) $(CORE_LIBS) -lm -o $@

mk-piclock-api: mk-piclock-api.c asset_store.c music_jobs.c font_catalog.c util.c weather_source_store.c weather_frames.c io_helpers.c hardware_profile.h ipc_protocol.h compiler_attrs.h asset_store.h music_jobs.h font_catalog.h util.h weather_source_store.h weather_frames.h weather_version.h io_helpers.h
	$(CC) $(API_CPPFLAGS) $(API_CFLAGS) mk-piclock-api.c weather_source_store.c weather_frames.c io_helpers.c asset_store.c music_jobs.c font_catalog.c util.c $(LDFLAGS) $(API_LIBS) -o $@

weather:
	$(MAKE) -C weather clean all

check-hardware:
	@if [ ! -e /dev/spidev0.0 ]; then echo "ERROR: /dev/spidev0.0 is unavailable. Enable the Armbian spi0 overlay and reboot."; exit 1; fi
	@if [ ! -e /dev/gpiochip0 ]; then echo "ERROR: /dev/gpiochip0 is unavailable."; exit 1; fi
	@if [ ! -e "$(I2C_DEVICE)" ]; then echo "ERROR: $(I2C_DEVICE) is unavailable. Enable the Armbian i2c0 overlay and reboot."; exit 1; fi
	@echo "BPI hardware preflight passed."

install: all
	@$(MAKE) --no-print-directory check-hardware
	-sudo systemctl stop mk-piclock-weather.path mk-piclock-weather.timer mk-piclock-weather.service mk-piclock-api.service mk-piclock-core.service
	sudo install -m 0644 hardware/mk-piclock.sysusers /usr/lib/sysusers.d/mk-piclock.conf
	sudo systemd-sysusers /usr/lib/sysusers.d/mk-piclock.conf
	@for group in audio spi gpio i2c; do getent group $$group >/dev/null && sudo usermod -a -G $$group mk-piclock-core || true; done
	sudo install -m 0644 hardware/60-mk-piclock-bpi.rules /etc/udev/rules.d/60-mk-piclock-bpi.rules
	-sudo udevadm control --reload-rules
	-sudo udevadm trigger --subsystem-match=spidev --action=change
	-sudo udevadm trigger --subsystem-match=gpio --action=change
	-sudo udevadm trigger --subsystem-match=i2c-dev --action=change
	sudo mkdir -p /opt/mk-piclock/assets/music /opt/mk-piclock/assets/music/.processing /opt/mk-piclock/assets/fonts /opt/mk-piclock/config
	sudo rm -rf /opt/mk-piclock/assets/images /opt/mk-piclock/assets/bedtime-images /opt/mk-piclock/assets/stories /opt/mk-piclock/assets/room-sensor
	sudo chown -R mk-piclock-api:mk-piclock /opt/mk-piclock/assets
	sudo chmod -R u=rwX,g=rX,o= /opt/mk-piclock/assets
	sudo chown -R mk-piclock-core:mk-piclock /opt/mk-piclock/config
	sudo chmod -R u=rwX,g=rwX,o= /opt/mk-piclock/config
	sudo install -m 0755 mk-piclock-core /opt/mk-piclock/mk-piclock-core
	sudo install -m 0755 mk-piclock-api /opt/mk-piclock/mk-piclock-api
	@printf '%s  %s\n' "$(DEFAULT_ALARM_SHA256)" assets/default-alarm.mp3 | sha256sum -c -
	sudo install -m 0640 -o root -g mk-piclock assets/default-alarm.mp3 /opt/mk-piclock/assets/default-alarm.mp3
	@printf '%s  %s\n' "$(MESSAGE_CHIME_SHA256)" assets/message-chime.mp3 | sha256sum -c -
	sudo install -m 0640 -o root -g mk-piclock assets/message-chime.mp3 /opt/mk-piclock/assets/message-chime.mp3
	sudo rm -rf /opt/mk-piclock/web /opt/mk-piclock/api
	sudo install -d -m 0755 /opt/mk-piclock/web /opt/mk-piclock/api
	sudo cp -a web/. /opt/mk-piclock/web/
	sudo chown -R root:root /opt/mk-piclock/web
	sudo chmod -R a=rX /opt/mk-piclock/web
	sudo install -m 0644 api/openapi-v1.json /opt/mk-piclock/api/openapi-v1.json
	sudo install -m 0644 mk-piclock-core.service /etc/systemd/system/mk-piclock-core.service
	sudo install -m 0644 mk-piclock-api.service /etc/systemd/system/mk-piclock-api.service
	sudo sh ./weather/install.sh --defer-start
	sudo systemctl daemon-reload
	sudo systemctl enable --now mk-piclock-core.service mk-piclock-api.service mk-piclock-weather.path mk-piclock-weather.timer
	@sudo systemctl start mk-piclock-weather.service || true
	@echo "Installed the BPI-M2 Zero adult clock and started its services."
	@echo "Boot configuration and Device Tree overlays were not changed."

uninstall:
	-sudo sh ./weather/uninstall.sh
	-sudo systemctl disable --now mk-piclock-api.service mk-piclock-core.service
	sudo rm -f /etc/systemd/system/mk-piclock-api.service /etc/systemd/system/mk-piclock-core.service /etc/udev/rules.d/60-mk-piclock-bpi.rules /usr/lib/sysusers.d/mk-piclock.conf
	-sudo udevadm control --reload-rules
	sudo rm -f /opt/mk-piclock/mk-piclock-api /opt/mk-piclock/mk-piclock-core /opt/mk-piclock/assets/default-alarm.mp3 /opt/mk-piclock/assets/message-chime.mp3
	sudo rm -rf /opt/mk-piclock/web /opt/mk-piclock/api /opt/mk-piclock/assets/room-sensor
	sudo systemctl daemon-reload

test:
	rm -f tests/test_aht10 tests/test_font_catalog
	$(MAKE) -C weather clean
	find . -type d -name '__pycache__' -prune -exec rm -rf {} +
	find . -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
	python3 tests/test_release_integrity.py
	python3 tests/test_bpi_port.py
	$(CC) $(C_STANDARD) $(WARNINGS) $(CFLAGS) tests/test_aht10.c aht10_sensor.c -lm -o tests/test_aht10
	./tests/test_aht10
	$(CC) $(C_STANDARD) $(WARNINGS) $(CFLAGS) tests/test_font_catalog.c font_catalog.c -o tests/test_font_catalog
	./tests/test_font_catalog
	python3 tests/test_room_panel.py
	python3 tests/test_install_docs.py
	python3 tests/test_alarm_safety.py
	python3 tests/test_current_guards.py
	python3 tests/test_web_password.py
	python3 tests/test_alarm_header.py
	python3 tests/test_clock_alignment.py
	python3 tests/test_diagnostics.py
	python3 tests/test_backup_restore.py
	python3 tests/test_weather_warning_rotation.py
	python3 tests/test_weather_panel_config.py
	@if command -v node >/dev/null 2>&1; then node tests/test_dashboard_weather.mjs && node tests/test_display_fonts.mjs; fi
	$(MAKE) -C weather test
	$(MAKE) --no-print-directory clean
	python3 tests/test_release_integrity.py

clean:
	rm -f mk-piclock-core mk-piclock-api tests/test_aht10 tests/test_font_catalog
	$(MAKE) -C weather clean
	find . -type d -name '__pycache__' -prune -exec rm -rf {} +
	find . -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
