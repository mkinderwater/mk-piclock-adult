#!/bin/bash
set -u

FAIL=0
RELEASE=/etc/bpi-zero-clock-release
MARKER=/proc/device-tree/bpi-zero-clock,hardware
COMPAT_MARKER=/proc/device-tree/mk-clock-adult,hardware
EXPECTED_PRODUCT=bpi-zero-clock
EXPECTED_VERSION=1.0.3
EXPECTED_HARDWARE=bpi-m2-zero-r1
EXPECTED_KERNEL=6.12.100+deb13-armmp
EXPECTED_AUDIO_DRIVER=snd-soc-max98357a
EXPECTED_AUDIO_COMPATIBLE=maxim,max98357a
EXPECTED_AUDIO_SOURCE=hardware-validated-local-build
EXPECTED_MODULE_SHA256=906b7ef831e199a7ae0dc1aa724251ea1763876298cdcd8564a25e70badaa3c6
EXPECTED_DTB_SHA256=7d54132d9b707ec62d5b72e08cd329b557a92940664e48164b5b8a8cfd5fcaff
EXPECTED_SD_CONTROL=codec-driver-pcm-trigger
EXPECTED_SD_GPIO=PA1
EXPECTED_SD_DELAY_MS=5
EXPECTED_MCLK_FS=256

ok()      { printf 'OK      %s\n' "$1"; }
missing() { printf 'MISSING %s\n' "$1"; FAIL=1; }
info()    { printf 'INFO    %s\n' "$1"; }

release_value() {
    local key="$1"
    [ -r "$RELEASE" ] || return 0
    sed -n "s/^${key}=//p" "$RELEASE" | head -1
}

property_hex() {
    local path="$1"
    [ -r "$path" ] || return 1
    od -An -tx1 -v "$path" 2>/dev/null | tr -d ' \n'
}

printf '%s\n' 'bpi-zero-clock hardware verification'
printf '%s\n' '------------------------------------'

PRODUCT="$(release_value PRODUCT)"
VERSION="$(release_value VERSION)"
CLOCK_HARDWARE="$(release_value CLOCK_HARDWARE)"
KERNEL_ABI="$(release_value KERNEL_ABI)"
CLOCK_DTB="$(release_value CLOCK_DTB)"
AUDIO_DRIVER="$(release_value AUDIO_CODEC_DRIVER)"
AUDIO_COMPATIBLE="$(release_value AUDIO_CODEC_COMPATIBLE)"
AUDIO_SOURCE="$(release_value AUDIO_DRIVER_SOURCE)"
MODULE_SHA_META="$(release_value MAX98357A_MODULE_SHA256)"
DTB_SHA_META="$(release_value MAX98357A_DTB_SHA256)"
SD_CONTROL="$(release_value MAX98357A_SD_CONTROL)"
SD_GPIO="$(release_value MAX98357A_SD_GPIO)"
SD_DELAY="$(release_value MAX98357A_SD_DELAY_MS)"
MCLK_FS="$(release_value MAX98357A_MCLK_FS)"
LEGACY_SPDIF="$(release_value LEGACY_SPDIF_CODEC)"
LEGACY_AMP_GATE="$(release_value LEGACY_AMP_GATE)"
LEGACY_AMP_GATE_FILES="$(release_value LEGACY_AMP_GATE_FILES)"

if [ "$PRODUCT" = "$EXPECTED_PRODUCT" ] && [ "$VERSION" = "$EXPECTED_VERSION" ] && [ "$CLOCK_HARDWARE" = "$EXPECTED_HARDWARE" ]; then
    ok "$PRODUCT $VERSION ($CLOCK_HARDWARE)"
else
    missing "bpi-zero-clock release marker"
    info "expected: $EXPECTED_PRODUCT $EXPECTED_VERSION / $EXPECTED_HARDWARE"
    info "active:   ${PRODUCT:-none} ${VERSION:-none} / ${CLOCK_HARDWARE:-none}"
fi

RUNNING_KERNEL="$(uname -r 2>/dev/null || true)"
if [ "$RUNNING_KERNEL" = "$EXPECTED_KERNEL" ] && [ "$KERNEL_ABI" = "$EXPECTED_KERNEL" ]; then
    ok "validated kernel ABI $EXPECTED_KERNEL"
else
    missing "validated kernel ABI"
    info "expected: $EXPECTED_KERNEL"
    info "running:  ${RUNNING_KERNEL:-unknown}"
    info "release:  ${KERNEL_ABI:-none}"
fi

if [ "$AUDIO_DRIVER" = "$EXPECTED_AUDIO_DRIVER" ] && \
   [ "$AUDIO_COMPATIBLE" = "$EXPECTED_AUDIO_COMPATIBLE" ] && \
   [ "$AUDIO_SOURCE" = "$EXPECTED_AUDIO_SOURCE" ] && \
   [ "$MODULE_SHA_META" = "$EXPECTED_MODULE_SHA256" ] && \
   [ "$DTB_SHA_META" = "$EXPECTED_DTB_SHA256" ] && \
   [ "$SD_CONTROL" = "$EXPECTED_SD_CONTROL" ] && \
   [ "$SD_GPIO" = "$EXPECTED_SD_GPIO" ] && \
   [ "$SD_DELAY" = "$EXPECTED_SD_DELAY_MS" ] && \
   [ "$MCLK_FS" = "$EXPECTED_MCLK_FS" ] && \
   [ "$LEGACY_SPDIF" = "removed" ] && \
   [ "$LEGACY_AMP_GATE" = "removed" ] && \
   [ "$LEGACY_AMP_GATE_FILES" = "absent-verified" ]; then
    ok "hardware-validated MAX98357A release metadata"
else
    missing "hardware-validated MAX98357A release metadata"
    info "driver:    ${AUDIO_DRIVER:-none}"
    info "compatible:${AUDIO_COMPATIBLE:-none}"
    info "source:    ${AUDIO_SOURCE:-none}"
    info "module sha:${MODULE_SHA_META:-none}"
    info "dtb sha:   ${DTB_SHA_META:-none}"
    info "sd control:${SD_CONTROL:-none}"
    info "sd gpio:   ${SD_GPIO:-none}"
    info "sd delay:  ${SD_DELAY:-none}"
    info "mclk-fs:   ${MCLK_FS:-none}"
    info "legacy spdif:    ${LEGACY_SPDIF:-none}"
    info "legacy amp gate: ${LEGACY_AMP_GATE:-none}"
    info "legacy files:    ${LEGACY_AMP_GATE_FILES:-none}"
fi

ACTIVE_MARKER=
if [ -r "$MARKER" ]; then
    ACTIVE_MARKER=$(tr -d '\0' < "$MARKER" 2>/dev/null || true)
elif [ -r "$COMPAT_MARKER" ]; then
    ACTIVE_MARKER=$(tr -d '\0' < "$COMPAT_MARKER" 2>/dev/null || true)
fi

if [ "$ACTIVE_MARKER" = "$EXPECTED_HARDWARE" ]; then
    ok "clock Device Tree active ($ACTIVE_MARKER)"
else
    missing "clock Device Tree marker"
    info "expected: $EXPECTED_HARDWARE"
    info "active:   ${ACTIVE_MARKER:-none}"
fi

# The image owns the spidev binder. Retry once if udev/systemd ordering was late.
if [ "$ACTIVE_MARKER" = "$EXPECTED_HARDWARE" ] && [ ! -e /dev/spidev0.0 ]; then
    if command -v systemctl >/dev/null 2>&1 && systemctl cat mk-piclock-spidev.service >/dev/null 2>&1; then
        info "retrying image-owned mk-piclock-spidev.service"
        systemctl restart mk-piclock-spidev.service >/dev/null 2>&1 || true
    elif [ -x /usr/local/sbin/mk-piclock-bind-spidev ]; then
        info "retrying image-owned spidev binder"
        /usr/local/sbin/mk-piclock-bind-spidev >/dev/null 2>&1 || true
    fi
fi

for dev in /dev/spidev0.0 /dev/gpiochip0 /dev/i2c-0; do
    if [ -e "$dev" ]; then ok "$dev"; else missing "$dev"; fi
done

if command -v modinfo >/dev/null 2>&1; then
    MODULE_PATH="$(modinfo -n snd-soc-max98357a 2>/dev/null || true)"
    MODULE_NAME="$(modinfo -F name snd-soc-max98357a 2>/dev/null || true)"
    MODULE_VERMAGIC="$(modinfo -F vermagic snd-soc-max98357a 2>/dev/null | awk '{print $1}')"
    if [ -n "$MODULE_PATH" ] && [ -r "$MODULE_PATH" ] && \
       [ "$MODULE_NAME" = "snd_soc_max98357a" ] && [ "$MODULE_VERMAGIC" = "$EXPECTED_KERNEL" ]; then
        ok "MAX98357A kernel module resolves ($MODULE_PATH)"
    else
        missing "MAX98357A kernel module resolution"
        info "path:     ${MODULE_PATH:-none}"
        info "name:     ${MODULE_NAME:-none}"
        info "vermagic: ${MODULE_VERMAGIC:-none}"
    fi

    if [ -n "$MODULE_PATH" ] && [ -r "$MODULE_PATH" ]; then
        MODULE_SHA_ACTUAL="$(sha256sum "$MODULE_PATH" 2>/dev/null | awk '{print $1}')"
        if [ "$MODULE_SHA_ACTUAL" = "$EXPECTED_MODULE_SHA256" ]; then
            ok "validated MAX98357A module SHA256"
        else
            missing "validated MAX98357A module SHA256"
            info "expected: $EXPECTED_MODULE_SHA256"
            info "active:   ${MODULE_SHA_ACTUAL:-unknown}"
        fi
    fi
else
    missing "modinfo utility"
fi

if grep -q '^snd_soc_max98357a ' /proc/modules 2>/dev/null; then
    ok "snd_soc_max98357a loaded"
else
    missing "snd_soc_max98357a loaded"
fi

if grep -q '^snd_soc_spdif_tx ' /proc/modules 2>/dev/null; then
    missing "legacy snd_soc_spdif_tx absent"
else
    ok "legacy snd_soc_spdif_tx absent"
fi

LIVE_CODEC=""
if [ -r /proc/device-tree/max98357a/compatible ]; then
    LIVE_CODEC="$(tr -d '\0' < /proc/device-tree/max98357a/compatible 2>/dev/null || true)"
fi
if [ "$LIVE_CODEC" = "$EXPECTED_AUDIO_COMPATIBLE" ]; then
    ok "live codec $LIVE_CODEC"
else
    missing "live MAX98357A codec"
    info "active: ${LIVE_CODEC:-none}"
fi

LIVE_SD_DELAY="$(property_hex /proc/device-tree/max98357a/sdmode-delay 2>/dev/null || true)"
if [ "$LIVE_SD_DELAY" = "00000005" ]; then
    ok "MAX98357A sdmode-delay 5 ms"
else
    missing "MAX98357A sdmode-delay"
    info "active property: ${LIVE_SD_DELAY:-none}"
fi

LIVE_MCLK_FS="$(property_hex /proc/device-tree/sound-max98357a/simple-audio-card,mclk-fs 2>/dev/null || true)"
if [ "$LIVE_MCLK_FS" = "00000100" ]; then
    ok "MAX98357A mclk-fs 256"
else
    missing "MAX98357A mclk-fs"
    info "active property: ${LIVE_MCLK_FS:-none}"
fi

LIVE_SD_GPIO="$(property_hex /proc/device-tree/max98357a/sdmode-gpios 2>/dev/null || true)"
case "$LIVE_SD_GPIO" in
    *000000000000000100000000) ok "MAX98357A SD/EN uses PA1" ;;
    *) missing "MAX98357A SD/EN PA1 ownership"; info "active property: ${LIVE_SD_GPIO:-none}" ;;
esac

if grep -Raq 'linux,spdif-dit' /proc/device-tree 2>/dev/null; then
    missing "legacy linux,spdif-dit absent from live Device Tree"
else
    ok "legacy linux,spdif-dit absent from live Device Tree"
fi

HOG=/proc/device-tree/soc/pinctrl@1c20800/mk-piclock-max98357a-enable-hog
if [ -e "$HOG" ]; then
    missing "legacy PA1 GPIO hog absent"
else
    ok "legacy PA1 GPIO hog absent"
fi

DTB_PATH=""
if [ -n "$CLOCK_DTB" ] && [ -n "$KERNEL_ABI" ]; then
    for candidate in \
        "/usr/lib/firmware/$KERNEL_ABI/device-tree/$CLOCK_DTB" \
        "/usr/lib/linux-image-$KERNEL_ABI/$CLOCK_DTB"; do
        if [ -r "$candidate" ]; then
            DTB_PATH="$candidate"
            break
        fi
    done
fi
if [ -n "$DTB_PATH" ]; then
    DTB_SHA_ACTUAL="$(sha256sum "$DTB_PATH" 2>/dev/null | awk '{print $1}')"
    if [ "$DTB_SHA_ACTUAL" = "$EXPECTED_DTB_SHA256" ]; then
        ok "validated clock DTB SHA256"
    else
        missing "validated clock DTB SHA256"
        info "path:     $DTB_PATH"
        info "expected: $EXPECTED_DTB_SHA256"
        info "active:   ${DTB_SHA_ACTUAL:-unknown}"
    fi
else
    missing "clock DTB file"
fi

if [ -r /proc/asound/cards ] && grep -q 'MAX98357A' /proc/asound/cards; then
    ok "MAX98357A ALSA card"
else
    missing "MAX98357A ALSA card"
fi

if [ -r /proc/asound/pcm ] && grep -q '1c22000.i2s-HiFi HiFi-0' /proc/asound/pcm; then
    ok "MAX98357A HiFi PCM path"
else
    missing "MAX98357A HiFi PCM path"
fi

LEGACY_GATE_PATHS=""
for path in \
    /etc/systemd/system/mk-clock-amp-gate.service \
    /etc/systemd/system/multi-user.target.wants/mk-clock-amp-gate.service \
    /usr/lib/systemd/system/mk-clock-amp-gate.service \
    /lib/systemd/system/mk-clock-amp-gate.service \
    /usr/local/sbin/mk-clock-amp-gate \
    /usr/local/bin/mk-clock-amp-gate \
    /root/mk-clock-amp-gate \
    /etc/modules-load.d/mk-clock-amp-gate.conf; do
    if [ -e "$path" ] || [ -L "$path" ]; then
        LEGACY_GATE_PATHS="${LEGACY_GATE_PATHS}${LEGACY_GATE_PATHS:+ }$path"
    fi
done

if [ -n "$LEGACY_GATE_PATHS" ]; then
    missing "legacy mk-clock-amp-gate files absent"
    for path in $LEGACY_GATE_PATHS; do
        info "legacy file: $path"
    done
else
    ok "legacy mk-clock-amp-gate files absent"
fi

if command -v systemctl >/dev/null 2>&1 && systemctl is-active --quiet mk-clock-amp-gate.service 2>/dev/null; then
    missing "legacy mk-clock-amp-gate inactive"
else
    ok "legacy mk-clock-amp-gate inactive"
fi

if [ "$FAIL" -ne 0 ]; then
    echo
    echo 'Hardware diagnostics:'
    [ -r "$RELEASE" ] && { echo; echo '[/etc/bpi-zero-clock-release]'; cat "$RELEASE"; }
    echo
    echo '[/proc/asound/cards]'
    cat /proc/asound/cards 2>/dev/null || echo 'unavailable'
    echo
    echo '[/proc/asound/pcm]'
    cat /proc/asound/pcm 2>/dev/null || echo 'unavailable'
    echo
    echo '[audio modules]'
    grep -E 'snd_soc_(max98357a|spdif_tx)|sun4i_i2s|snd_soc_simple_card' /proc/modules 2>/dev/null || true
    echo
    echo '[relevant kernel messages]'
    dmesg 2>/dev/null | grep -iE 'spi|spidev|i2c|i2s|max98357|simple-audio|asoc|snd' | tail -100 || true
    echo
    echo 'ERROR: the bpi-zero-clock hardware layer is not ready.'
    echo 'Flash/boot bpi-zero-clock 1.0.3 and allow first boot to complete before installing mk-clock-adult.'
    echo 'The application installer uses the image-owned hardware layer and does not patch Device Tree or build kernel modules.'
    exit 1
fi

echo
echo 'All required bpi-zero-clock 1.0.3 hardware is ready.'
exit 0
