#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root, or use: sudo ./install.sh" >&2
    exit 1
fi

BASE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEFER_START=0
if [ "${1:-}" = "--defer-start" ]; then
    DEFER_START=1
    shift
fi
if [ "$#" -ne 0 ]; then
    echo "Usage: ./install.sh [--defer-start]" >&2
    exit 2
fi

BIN="$BASE_DIR/build/mk-piclock-weather"
[ -x "$BIN" ] || {
    echo "Weather binary is missing. Run make first." >&2
    exit 1
}

id mk-piclock-api >/dev/null 2>&1 || {
    echo "mk-piclock-api user is missing." >&2
    exit 1
}
getent group mk-piclock >/dev/null 2>&1 || {
    echo "mk-piclock group is missing." >&2
    exit 1
}

set -- "$BASE_DIR"/assets/oled-icons/[0-9][0-9].raw
[ "$#" -eq 49 ] || {
    echo "Weather icon pack must contain exactly 49 RAW files; found $#." >&2
    exit 1
}
for icon in "$@"; do
    [ "$(stat -c %s "$icon")" -eq 512 ] || {
        echo "Invalid weather icon: $icon" >&2
        exit 1
    }
done

systemctl stop mk-piclock-weather.path mk-piclock-weather.timer \
    mk-piclock-weather.service 2>/dev/null || true

install -d -m 0755 /usr/local/lib/mk-piclock-weather
install -m 0755 "$BIN" /usr/local/lib/mk-piclock-weather/mk-piclock-weather
rm -f /usr/local/lib/mk-piclock-weather/fetch_weather.py

install -m 0644 "$BASE_DIR/config/mk-piclock-weather.env" \
    /etc/default/mk-piclock-weather
install -d -o mk-piclock-api -g mk-piclock -m 0770 \
    /var/lib/mk-piclock-weather
install -d -m 0755 /usr/local/share/mk-piclock-weather/icons
install -d -o root -g mk-piclock -m 0775 /run/mk-piclock
install -d -o mk-piclock-api -g mk-piclock -m 0770 \
    /run/mk-piclock/weather-icons
install -m 0644 "$BASE_DIR"/assets/oled-icons/*.raw \
    /usr/local/share/mk-piclock-weather/icons/

if [ ! -f /var/lib/mk-piclock-weather/weather-source.url ]; then
    install -o mk-piclock-api -g mk-piclock -m 0644 \
        "$BASE_DIR/config/weather-source.url" \
        /var/lib/mk-piclock-weather/weather-source.url
fi
if [ ! -f /var/lib/mk-piclock-weather/weather-frames.conf ]; then
    install -o mk-piclock-api -g mk-piclock -m 0644 \
        "$BASE_DIR/config/weather-frames.conf" \
        /var/lib/mk-piclock-weather/weather-frames.conf
fi
if [ ! -f /var/lib/mk-piclock-weather/status.json ]; then
    printf '%s\n' '{"schema_version":1,"ok":false,"result":"pending","message":"No weather update has run yet."}' \
        > /var/lib/mk-piclock-weather/status.json
fi
if [ ! -f /var/lib/mk-piclock-weather/activity.json ]; then
    printf '%s\n' '{"schema_version":1,"entries":[]}' \
        > /var/lib/mk-piclock-weather/activity.json
fi
chown mk-piclock-api:mk-piclock /var/lib/mk-piclock-weather/*
chmod 0644 /var/lib/mk-piclock-weather/*

install -m 0644 "$BASE_DIR/systemd/mk-piclock-weather.service" \
    /etc/systemd/system/mk-piclock-weather.service
install -m 0644 "$BASE_DIR/systemd/mk-piclock-weather.timer" \
    /etc/systemd/system/mk-piclock-weather.timer
install -m 0644 "$BASE_DIR/systemd/mk-piclock-weather.path" \
    /etc/systemd/system/mk-piclock-weather.path
install -m 0644 "$BASE_DIR/systemd/mk-piclock-weather.tmpfiles" \
    /etc/tmpfiles.d/mk-piclock-weather.conf
install -d -m 0755 /etc/systemd/system/mk-piclock-api.service.d
install -m 0644 \
    "$BASE_DIR/systemd/mk-piclock-api.service.d/weather-source.conf" \
    /etc/systemd/system/mk-piclock-api.service.d/weather-source.conf

systemd-tmpfiles --create /etc/tmpfiles.d/mk-piclock-weather.conf
systemctl daemon-reload

if [ "$DEFER_START" -eq 0 ]; then
    systemctl try-restart mk-piclock-api.service
    systemctl enable --now mk-piclock-weather.path mk-piclock-weather.timer
    systemctl start mk-piclock-weather.service || true
fi
