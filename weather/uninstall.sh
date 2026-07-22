#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo ./uninstall.sh" >&2
    exit 1
fi

systemctl disable --now mk-piclock-weather.path mk-piclock-weather.timer \
    2>/dev/null || true
systemctl stop mk-piclock-weather.service 2>/dev/null || true

rm -f \
    /etc/systemd/system/mk-piclock-weather.path \
    /etc/systemd/system/mk-piclock-weather.service \
    /etc/systemd/system/mk-piclock-weather.timer \
    /etc/tmpfiles.d/mk-piclock-weather.conf \
    /etc/systemd/system/mk-piclock-api.service.d/weather-source.conf
rmdir /etc/systemd/system/mk-piclock-api.service.d 2>/dev/null || true
rm -rf \
    /usr/local/lib/mk-piclock-weather \
    /usr/local/share/mk-piclock-weather \
    /run/mk-piclock/weather-icons
rm -f /run/mk-piclock/weather.json

systemctl daemon-reload
systemctl reset-failed mk-piclock-weather.service 2>/dev/null || true
systemctl try-restart mk-piclock-api.service 2>/dev/null || true
