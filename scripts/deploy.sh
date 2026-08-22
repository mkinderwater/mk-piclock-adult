#!/bin/bash
set -Eeuo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: deployment must run as root." >&2
    exit 1
fi

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"
VERSION=$(tr -d '[:space:]' < VERSION)
API_VERSION=$(sed -n 's/^#define API_VERSION "\([^"]*\)"/\1/p' mk-piclock-api.c | head -1)
IPC_VERSION=$(sed -n 's/^#define MP_IPC_VERSION \([0-9][0-9]*\)u/\1/p' ipc_protocol.h | head -1)
WEATHER_VERSION=$(sed -n 's/^#define MP_WEATHER_VERSION "\([^"]*\)"/\1/p' weather_version.h | head -1)

for f in mk-piclock-core mk-piclock-api weather/build/mk-piclock-weather; do
    [ -x "$f" ] || { echo "ERROR: build output missing: $f" >&2; exit 1; }
done

INSTALLED_VERSION=""
if [ -r /etc/mk-clock-adult-release ]; then
    INSTALLED_VERSION=$(sed -n 's/^VERSION=//p' /etc/mk-clock-adult-release | head -1)
elif [ -r /opt/mk-piclock/VERSION ]; then
    INSTALLED_VERSION=$(tr -d '[:space:]' < /opt/mk-piclock/VERSION)
elif [ -r /opt/mk-piclock/web/assets/js/app.js ]; then
    INSTALLED_VERSION=$(sed -n "s/.*GUI_VERSION = 'mk-clock-adult-\([0-9][0-9.]*\)-bpi-m2-zero-r1'.*/\1/p" /opt/mk-piclock/web/assets/js/app.js | head -1)
fi
if [ -n "$INSTALLED_VERSION" ]; then
    if [ "$INSTALLED_VERSION" = "$VERSION" ]; then
        echo "Reinstall detected: ${VERSION}"
    else
        echo "Upgrade detected: ${INSTALLED_VERSION} -> ${VERSION}"
    fi
else
    echo "Fresh install detected"
fi

OWNED_PATHS=(
    opt/mk-piclock/mk-piclock-core
    opt/mk-piclock/mk-piclock-api
    opt/mk-piclock/VERSION
    opt/mk-piclock/web
    opt/mk-piclock/assets/default-alarm.mp3
    opt/mk-piclock/assets/message-chime.mp3
    usr/local/lib/mk-piclock-weather
    usr/local/share/mk-piclock-weather/icons
    etc/default/mk-piclock-weather
    etc/systemd/system/mk-piclock-core.service
    etc/systemd/system/mk-piclock-api.service
    etc/systemd/system/mk-piclock-weather.service
    etc/systemd/system/mk-piclock-weather.timer
    etc/systemd/system/mk-piclock-weather.path
    etc/systemd/system/mk-piclock-api.service.d/weather-source.conf
    etc/tmpfiles.d/mk-piclock-weather.conf
    etc/udev/rules.d/60-mk-piclock-bpi.rules
    etc/polkit-1/rules.d/49-mk-clock-adult-timezone.rules
    etc/polkit-1/rules.d/49-mk-clock-adult-system.rules
    etc/systemd/system/mk-clock-system-config.service
    usr/local/libexec/mk-clock-system-helper
    usr/lib/sysusers.d/mk-piclock.conf
    etc/mk-clock-adult-release
)
# Persistent user data is intentionally outside release ownership.
# Assets below music/podcasts/fonts and every file below config survive upgrades.
# Keep config as a directory-level persistence boundary so future settings do not
# need to be added to an installer allowlist.
ROLLBACK_STATE_PATHS=(
    opt/mk-piclock/config
)

# Preserve/remove only unknown legacy top-level asset files. Persistent asset
# directories are never classified as legacy and are never removed on upgrade.
LEGACY_PATHS=()
if [ -d /opt/mk-piclock/assets ]; then
    while IFS= read -r -d '' path; do
        base=$(basename "$path")
        case "$base" in
            default-alarm.mp3|message-chime.mp3) continue ;;
        esac
        LEGACY_PATHS+=("${path#/}")
    done < <(find /opt/mk-piclock/assets -mindepth 1 -maxdepth 1 \
        \( -type f -o -type l \) -print0 2>/dev/null)
fi
UNITS=(mk-piclock-core.service mk-piclock-api.service mk-piclock-weather.path mk-piclock-weather.timer)
BACKUP_DIR=$(mktemp -d /var/tmp/mk-clock-adult-install.XXXXXX)
BACKUP_TAR="$BACKUP_DIR/previous.tar"
ACTIVE_BEFORE="$BACKUP_DIR/active"
ENABLED_BEFORE="$BACKUP_DIR/enabled"
: > "$ACTIVE_BEFORE"
: > "$ENABLED_BEFORE"

for unit in "${UNITS[@]}"; do
    systemctl is-active --quiet "$unit" 2>/dev/null && echo "$unit" >> "$ACTIVE_BEFORE" || true
    systemctl is-enabled --quiet "$unit" 2>/dev/null && echo "$unit" >> "$ENABLED_BEFORE" || true
done

EXISTING=()
for path in "${OWNED_PATHS[@]}" "${LEGACY_PATHS[@]}" "${ROLLBACK_STATE_PATHS[@]}"; do
    [ -e "/$path" ] || [ -L "/$path" ] || continue
    EXISTING+=("$path")
done
if [ "${#EXISTING[@]}" -gt 0 ]; then
    tar -cpf "$BACKUP_TAR" -C / "${EXISTING[@]}"
else
    tar -cpf "$BACKUP_TAR" -C / --files-from /dev/null
fi

rollback() {
    local rc=$?
    trap - ERR INT TERM
    set +e
    echo "ERROR   Installation failed; restoring previous release" >&2
    systemctl disable --now "${UNITS[@]}" >/dev/null 2>&1 || true
    systemctl stop mk-piclock-weather.service >/dev/null 2>&1 || true
    for path in "${OWNED_PATHS[@]}" "${LEGACY_PATHS[@]}" "${ROLLBACK_STATE_PATHS[@]}"; do rm -rf "/$path"; done
    tar -xpf "$BACKUP_TAR" -C / >/dev/null 2>&1 || true
    systemctl daemon-reload >/dev/null 2>&1 || true
    while IFS= read -r unit; do [ -n "$unit" ] && systemctl enable "$unit" >/dev/null 2>&1 || true; done < "$ENABLED_BEFORE"
    while IFS= read -r unit; do [ -n "$unit" ] && systemctl start "$unit" >/dev/null 2>&1 || true; done < "$ACTIVE_BEFORE"
    rm -rf "$BACKUP_DIR"
    echo "ROLLBACK Previous installation restored" >&2
    exit "$rc"
}
trap rollback ERR INT TERM

echo "Stopping application services..."
systemctl stop mk-piclock-weather.path mk-piclock-weather.timer mk-piclock-weather.service mk-piclock-api.service mk-piclock-core.service 2>/dev/null || true

# Remove files no longer owned by the current release. They were included in the
# transaction backup above, so rollback can still restore the previous install.
for path in "${LEGACY_PATHS[@]}"; do
    rm -rf "/$path"
done

install -m 0644 hardware/mk-piclock.sysusers /usr/lib/sysusers.d/mk-piclock.conf
systemd-sysusers /usr/lib/sysusers.d/mk-piclock.conf
for group in audio spi gpio i2c; do getent group "$group" >/dev/null && usermod -a -G "$group" mk-piclock-core || true; done
install -m 0644 hardware/60-mk-piclock-bpi.rules /etc/udev/rules.d/60-mk-piclock-bpi.rules
install -d -m 0755 /etc/polkit-1/rules.d /usr/local/libexec
rm -f /etc/polkit-1/rules.d/49-mk-clock-adult-timezone.rules
install -m 0644 packaging/49-mk-clock-adult-system.rules /etc/polkit-1/rules.d/49-mk-clock-adult-system.rules
install -m 0755 packaging/mk-clock-system-helper /usr/local/libexec/mk-clock-system-helper
install -m 0644 packaging/mk-clock-system-config.service /etc/systemd/system/mk-clock-system-config.service
udevadm control --reload-rules || true
udevadm trigger --subsystem-match=spidev --action=change || true
udevadm trigger --subsystem-match=gpio --action=change || true
udevadm trigger --subsystem-match=i2c-dev --action=change || true

mkdir -p /opt/mk-piclock/assets/music /opt/mk-piclock/assets/music/.processing /opt/mk-piclock/assets/podcasts /opt/mk-piclock/assets/podcasts/upload /opt/mk-piclock/assets/fonts /opt/mk-piclock/config
chown -R mk-piclock-api:mk-piclock /opt/mk-piclock/assets
chmod -R u=rwX,g=rX,o= /opt/mk-piclock/assets
chmod 0770 /opt/mk-piclock/assets/podcasts/upload
chown -R mk-piclock-core:mk-piclock /opt/mk-piclock/config
chmod -R u=rwX,g=rwX,o= /opt/mk-piclock/config

install -m 0755 mk-piclock-core /opt/mk-piclock/mk-piclock-core
install -m 0755 mk-piclock-api /opt/mk-piclock/mk-piclock-api
install -m 0644 VERSION /opt/mk-piclock/VERSION
install -m 0640 -o root -g mk-piclock assets/default-alarm.mp3 /opt/mk-piclock/assets/default-alarm.mp3
install -m 0640 -o root -g mk-piclock assets/message-chime.mp3 /opt/mk-piclock/assets/message-chime.mp3
rm -rf /opt/mk-piclock/web
install -d -m 0755 /opt/mk-piclock/web
cp -a web/. /opt/mk-piclock/web/
chown -R root:root /opt/mk-piclock/web
chmod -R a=rX /opt/mk-piclock/web

install -m 0644 mk-piclock-core.service /etc/systemd/system/mk-piclock-core.service
install -m 0644 mk-piclock-api.service /etc/systemd/system/mk-piclock-api.service
sh ./weather/install.sh --defer-start --skip-validation

systemctl daemon-reload
systemd-tmpfiles --create /etc/tmpfiles.d/mk-piclock-weather.conf
systemctl enable mk-piclock-core.service mk-piclock-api.service mk-piclock-weather.path mk-piclock-weather.timer
systemctl start mk-piclock-core.service mk-piclock-api.service mk-piclock-weather.path mk-piclock-weather.timer
if ! systemctl start mk-piclock-weather.service; then
    echo "WARN    Initial weather refresh failed; scheduled refresh remains enabled"
fi

echo "Health check..."
./scripts/verify-install.sh

cat > /etc/mk-clock-adult-release <<EOF_RELEASE
PRODUCT=mk-clock-adult
VERSION=${VERSION}
HARDWARE=bpi-m2-zero-r1
API_VERSION=${API_VERSION}
IPC_VERSION=${IPC_VERSION}
WEATHER_VERSION=${WEATHER_VERSION}
BASE_IMAGE=bpi-zero-clock
BASE_IMAGE_VERSION=1.0.4-preview36
INSTALLED_AT=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
CORE_SHA256=$(sha256sum /opt/mk-piclock/mk-piclock-core | awk '{print $1}')
API_SHA256=$(sha256sum /opt/mk-piclock/mk-piclock-api | awk '{print $1}')
WEATHER_SHA256=$(sha256sum /usr/local/lib/mk-piclock-weather/mk-piclock-weather | awk '{print $1}')
EOF_RELEASE
chmod 0644 /etc/mk-clock-adult-release

# Retired application assets are removed only after the new release passes health checks.
rm -f /opt/mk-piclock/config/wifi-scan.tsv /run/mk-clock-adult/wifi-pending
rm -rf /opt/mk-piclock/assets/images /opt/mk-piclock/assets/bedtime-images /opt/mk-piclock/assets/stories /opt/mk-piclock/assets/room-sensor /opt/mk-piclock/api

trap - ERR INT TERM
rm -rf "$BACKUP_DIR"
echo "OK      mk-clock-adult ${VERSION} deployment verified"
