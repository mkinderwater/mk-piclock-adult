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
DEBIAN_PACKAGE_FILE := packaging/debian-packages.txt
DEBIAN_PACKAGES := $(shell sed -e 's/#.*$$//' -e '/^[[:space:]]*$$/d' $(DEBIAN_PACKAGE_FILE) 2>/dev/null)

CORE_CPPFLAGS = $(CPPFLAGS) -I/usr/include/freetype2
CORE_CFLAGS = $(C_STANDARD) $(WARNINGS) $(CFLAGS)
CORE_LIBS ?= -lgpiod -lfreetype -lasound -lmpg123 -pthread

MHD_CFLAGS := $(shell $(PKG_CONFIG) --cflags libmicrohttpd 2>/dev/null)
MHD_LIBS := $(shell $(PKG_CONFIG) --libs libmicrohttpd 2>/dev/null)
API_CPPFLAGS = $(CPPFLAGS) $(MHD_CFLAGS) -I/usr/include/freetype2
API_CFLAGS = $(C_STANDARD) $(WARNINGS) $(CFLAGS)
API_LIBS ?= $(if $(strip $(MHD_LIBS)),$(MHD_LIBS),-lmicrohttpd) -lfreetype -lmpg123 -lmp3lame -pthread

.PHONY: all build weather service-watchdog-check clean require-root check-deps check-hardware validate-release validate-build install uninstall package-release

all: build

build: mk-piclock-core mk-piclock-api weather

mk-piclock-core: mk-piclock.c service_watchdog.c aht10_sensor.c font_catalog.c io_helpers.c util.c hardware_profile.h interaction_profile.h service_watchdog.h ipc_protocol.h compiler_attrs.h aht10_sensor.h font_catalog.h io_helpers.h util.h
	$(CC) $(CORE_CPPFLAGS) $(CORE_CFLAGS) mk-piclock.c service_watchdog.c aht10_sensor.c font_catalog.c io_helpers.c util.c $(LDFLAGS) $(CORE_LIBS) -lm -o $@

service-watchdog-check:
	$(CC) $(CPPFLAGS) $(C_STANDARD) -Wall -Wextra -Wformat=2 -Werror $(CFLAGS) -I. -c service_watchdog.c -o /tmp/mk-clock-service-watchdog.o

mk-piclock-api: mk-piclock-api.c asset_store.c music_jobs.c podcast_import.c font_catalog.c util.c weather_source_store.c weather_frames.c io_helpers.c hardware_profile.h interaction_profile.h ipc_protocol.h compiler_attrs.h asset_store.h music_jobs.h podcast_import.h font_catalog.h util.h weather_source_store.h weather_frames.h weather_version.h io_helpers.h
	$(CC) $(API_CPPFLAGS) $(API_CFLAGS) mk-piclock-api.c weather_source_store.c weather_frames.c io_helpers.c asset_store.c music_jobs.c podcast_import.c font_catalog.c util.c $(LDFLAGS) $(API_LIBS) -o $@

weather:
	$(MAKE) -C weather clean all

require-root:
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "ERROR: this target must be run as root."; \
		echo "Log in as root or run: su -"; \
		exit 1; \
	fi

check-deps:
	@test -r $(DEBIAN_PACKAGE_FILE) || { echo "ERROR: missing $(DEBIAN_PACKAGE_FILE)"; exit 1; }
	@missing=""; \
	for pkg in $(DEBIAN_PACKAGES); do \
		dpkg-query -W -f='$${Status}' "$$pkg" 2>/dev/null | grep -q '^install ok installed$$' || missing="$$missing $$pkg"; \
	done; \
	if [ -n "$$missing" ]; then \
		echo "ERROR: required Debian packages are missing:"; \
		for pkg in $$missing; do echo "  $$pkg"; done; \
		echo; \
		echo "Run ./install.sh to install only the missing dependencies."; \
		exit 1; \
	fi
	@echo "OK      Required Debian packages are installed"

check-hardware: require-root
	@/bin/bash hardware/verify-bpi-hardware.sh

validate-release:
	@test -s VERSION || { echo "ERROR: VERSION is missing or empty"; exit 1; }
	@version="$$(tr -d '[:space:]' < VERSION)"; expected="mk-clock-adult-$${version}-bpi-m2-zero-r1"; gui="$$(sed -n "s/^const GUI_VERSION = '\([^']*\)';/\1/p" web/assets/js/app.js | head -1)"; hw="$$(sed -n 's/^#define MP_PRODUCT_VERSION "\([^"]*\)"/\1/p' hardware_profile.h | head -1)"; [ "$$gui" = "$$expected" ] && [ "$$hw" = "$$expected" ] || { echo "ERROR: product identity mismatch"; exit 1; }; echo "OK      Product identity $$expected"
	@api="$$(sed -n 's/^#define API_VERSION "\([^"]*\)"/\1/p' mk-piclock-api.c | head -1)"; gui="$$(sed -n "s/^const REQUIRED_API_VERSION = '\([^']*\)';/\1/p" web/assets/js/app.js | head -1)"; [ "$$api" = "$$gui" ] || { echo "ERROR: GUI/API version mismatch"; exit 1; }; echo "OK      GUI/API version contract v$$api"
	@grep -q 'Private core/API IPC protocol: v35' README.md && grep -q 'HTTP API: v1.62' README.md || { echo "ERROR: README protocol compatibility is stale"; exit 1; }
	@for script in install.sh hardware/verify-bpi-hardware.sh packaging/build-release.sh scripts/deploy.sh scripts/verify-install.sh weather/install.sh weather/uninstall.sh; do test -x "$$script" || { echo "ERROR: required script is not executable: $$script"; exit 1; }; done; echo "OK      Release script permissions validated"
	@printf '%s  %s\n' "$(DEFAULT_ALARM_SHA256)" assets/default-alarm.mp3 | sha256sum -c -
	@printf '%s  %s\n' "$(MESSAGE_CHIME_SHA256)" assets/message-chime.mp3 | sha256sum -c -
	@sh ./weather/install.sh --validate-only
	@grep -q 'MP_GPIO_TOUCH 17' hardware_profile.h || { echo "ERROR: touch input is not PA17"; exit 1; }
	@grep -q 'EXPECTED_VERSION=1.0.4-preview36' hardware/verify-bpi-hardware.sh && grep -q 'EXPECTED_KERNEL=6.12.101+deb13-armmp' hardware/verify-bpi-hardware.sh && ! grep -q 'EXPECTED_.*SHA256=' hardware/verify-bpi-hardware.sh || { echo "ERROR: preview36 playback hardware contract is stale"; exit 1; }
	@grep -q 'ROLLBACK_STATE_PATHS=(' scripts/deploy.sh && grep -q 'opt/mk-piclock/config' scripts/deploy.sh && ! grep -A20 'if \[ -d /opt/mk-piclock/config \]' scripts/deploy.sh | grep -q 'LEGACY_PATHS' || { echo "ERROR: upgrade persistence boundary for /opt/mk-piclock/config is missing or unsafe"; exit 1; }
	@! grep -qiE 'ICS-43434|MP_IPC_OP_VOICE|mp_voice_' mk-piclock.c mk-piclock-api.c ipc_protocol.h hardware_profile.h interaction_profile.h || { echo "ERROR: retired capture code survived in active C/header sources"; exit 1; }
	@test ! -e voice_capture.c -a ! -e voice_dsp.c || { echo "ERROR: retired capture source files survived"; exit 1; }
	@echo "OK      Playback-only release payload validated"

validate-build:
	@test -x mk-piclock-core || { echo "ERROR: mk-piclock-core was not built"; exit 1; }
	@test -x mk-piclock-api || { echo "ERROR: mk-piclock-api was not built"; exit 1; }
	@test -x weather/build/mk-piclock-weather || { echo "ERROR: weather binary was not built"; exit 1; }
	@echo "OK      Build outputs validated"

install: require-root
	@./install.sh

package-release:
	@./packaging/build-release.sh

uninstall: require-root
	-sh ./weather/uninstall.sh
	-systemctl disable --now mk-piclock-api.service mk-piclock-core.service
	rm -f /etc/systemd/system/mk-piclock-api.service /etc/systemd/system/mk-piclock-core.service
	rm -f /etc/udev/rules.d/60-mk-piclock-bpi.rules /etc/polkit-1/rules.d/49-mk-clock-adult-timezone.rules /etc/polkit-1/rules.d/49-mk-clock-adult-system.rules /usr/lib/sysusers.d/mk-piclock.conf
	rm -f /etc/systemd/system/mk-clock-system-config.service /usr/local/libexec/mk-clock-system-helper
	-udevadm control --reload-rules
	rm -f /opt/mk-piclock/mk-piclock-api /opt/mk-piclock/mk-piclock-core /opt/mk-piclock/VERSION /opt/mk-piclock/assets/default-alarm.mp3 /opt/mk-piclock/assets/message-chime.mp3
	rm -f /etc/mk-clock-adult-release
	rm -rf /opt/mk-piclock/web /opt/mk-piclock/api /opt/mk-piclock/config /opt/mk-piclock/assets/room-sensor
	systemctl daemon-reload
	@echo "Removed mk-clock-adult application files. bpi-zero-clock hardware remains installed and unchanged."

clean:
	rm -f mk-piclock-core mk-piclock-api
	$(MAKE) -C weather clean
