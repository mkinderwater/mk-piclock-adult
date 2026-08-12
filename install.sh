#!/bin/bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: run this installer as root." >&2
    exit 1
fi

cd "$(dirname "$0")"

echo "mk-clock-adult 2.3.22 installer"
echo "Checking bpi-zero-clock 1.0.3 hardware baseline..."
./hardware/verify-bpi-hardware.sh

echo "Installing required Debian packages..."
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    gcc make libc6-dev pkg-config ca-certificates tzdata python3 \
    fonts-dejavu-mono \
    bluez bluez-alsa-utils \
    libgpiod-dev libfreetype-dev libasound2-dev libmpg123-dev \
    libmicrohttpd-dev libmp3lame-dev libcurl4-openssl-dev libjson-c-dev

echo "Building and installing mk-clock-adult..."
make install

echo
IP=$(hostname -I 2>/dev/null | awk '{print $1}')
echo "mk-clock-adult 2.3.22 is installed and running."
if [ -n "${IP:-}" ]; then
    echo "Open: http://${IP}:8080/"
else
    echo "Open: http://<clock-ip>:8080/"
fi
