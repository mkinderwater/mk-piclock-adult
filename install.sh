#!/bin/bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: run this installer as root." >&2
    exit 1
fi

cd "$(dirname "$0")"
VERSION=$(tr -d '[:space:]' < VERSION)
LOG_FILE=/var/log/mk-clock-adult-install.log
install -d -m 0755 /var/log
touch "$LOG_FILE"
chmod 0640 "$LOG_FILE"
exec > >(tee -a "$LOG_FILE") 2>&1

echo
echo "[$(date -u '+%Y-%m-%dT%H:%M:%SZ')] mk-clock-adult ${VERSION} installer"
echo "Preflight: bpi-zero-clock 1.0.4-preview36 playback hardware baseline"
echo "Wiring: TTP223B VCC = 3.3 V / physical pin 17"
echo "Wiring: TTP223B GND = GND / physical pin 39"
echo "Wiring: TTP223B OUT = PA17 / physical pin 37 / gpiochip0 line 17"
echo "Wiring: legacy PA21 / physical pin 38 touch OUT must be moved to pin 37"

# bpi-zero-clock 1.0.4-preview36 is a 2026 playback-only appliance image. A date before
# 2026-01-01 indicates that the board clock has not been initialized.
# Do not require NTP: a manually set or otherwise correct offline clock is valid.
MIN_SANE_EPOCH=1767225600
CURRENT_EPOCH=$(date -u +%s 2>/dev/null || printf '0')
if ! [[ "$CURRENT_EPOCH" =~ ^[0-9]+$ ]] || [ "$CURRENT_EPOCH" -lt "$MIN_SANE_EPOCH" ]; then
    echo "ERROR: system clock is not sane ($(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo unknown))." >&2
    echo "Set the system date/time, then rerun the installer." >&2
    exit 1
fi
echo "OK      System clock appears sane ($(date -u '+%Y-%m-%dT%H:%M:%SZ'))"

./hardware/verify-bpi-hardware.sh

mapfile -t REQUIRED_PACKAGES < <(sed -e 's/#.*$//' -e '/^[[:space:]]*$/d' packaging/debian-packages.txt)
MISSING_PACKAGES=()
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q '^install ok installed$' || MISSING_PACKAGES+=("$pkg")
done

if [ "${#MISSING_PACKAGES[@]}" -eq 0 ]; then
    echo "OK      All required Debian packages are installed"
    echo "SKIP    apt-get update not required"
else
    echo "Installing missing Debian packages: ${MISSING_PACKAGES[*]}"
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${MISSING_PACKAGES[@]}"
fi

echo "Validating release payload..."
make --no-print-directory validate-release

echo "Building mk-clock-adult..."
make --no-print-directory all
make --no-print-directory validate-build

echo "Deploying mk-clock-adult ${VERSION}..."
./scripts/deploy.sh

echo
IP=$(hostname -I 2>/dev/null | awk '{print $1}')
echo "mk-clock-adult ${VERSION} installed successfully."
if [ -n "${IP:-}" ]; then
    echo "Open: http://${IP}:8080/"
else
    echo "Open: http://<clock-ip>:8080/"
fi
echo "Install log: ${LOG_FILE}"
