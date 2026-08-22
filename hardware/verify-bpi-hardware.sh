#!/bin/bash
set -u

FAIL=0
RELEASE=/etc/bpi-zero-clock-release
FIRSTBOOT_STATUS=/var/lib/bpi-zero-wbuild-status
MARKER=/proc/device-tree/bpi-zero-clock,hardware
COMPAT_MARKER=/proc/device-tree/mk-clock-adult,hardware
EXPECTED_PRODUCT=bpi-zero-clock
EXPECTED_VERSION=1.0.4-preview36
EXPECTED_HARDWARE=bpi-m2-zero-r1
EXPECTED_KERNEL=6.12.101+deb13-armmp
EXPECTED_AUDIO_DRIVER=snd-soc-max98357a
EXPECTED_AUDIO_COMPATIBLE=maxim,max98357a
EXPECTED_AUDIO_SOURCE=hardware-validated-source-rebuilt-for-6.12.101
EXPECTED_SD_CONTROL=codec-driver-pcm-trigger
EXPECTED_SD_GPIO=PA1
EXPECTED_SD_DELAY_MS=5
EXPECTED_MCLK_FS=256
EXPECTED_JOURNAL_STORAGE=volatile
EXPECTED_JOURNAL_RUNTIME_MAX_USE=16M
EXPECTED_TMP_MOUNT=tmpfs-64M
EXPECTED_RUNTIME_WATCHDOG=16s
EXPECTED_REBOOT_WATCHDOG=16s

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

is_sha256() {
    printf '%s' "$1" | grep -Eq '^[0-9a-f]{64}$'
}

printf '%s\n' 'bpi-zero-clock playback hardware verification'
printf '%s\n' '---------------------------------------------'

PRODUCT="$(release_value PRODUCT)"
VERSION="$(release_value VERSION)"
CLOCK_HARDWARE="$(release_value CLOCK_HARDWARE)"
KERNEL_ABI="$(release_value KERNEL_ABI)"
CLOCK_DTB="$(release_value CLOCK_DTB)"
AUDIO_MODE="$(release_value AUDIO_MODE)"
AUDIO_CAPTURE="$(release_value AUDIO_CAPTURE)"
AUDIO_DRIVER="$(release_value AUDIO_CODEC_DRIVER)"
AUDIO_COMPATIBLE="$(release_value AUDIO_CODEC_COMPATIBLE)"
AUDIO_SOURCE="$(release_value AUDIO_DRIVER_SOURCE)"
MODULE_PATH="$(release_value MAX98357A_MODULE)"
MODULE_SHA_META="$(release_value MAX98357A_MODULE_SHA256)"
DTB_SHA_META="$(release_value MAX98357A_DTB_SHA256)"
SD_CONTROL="$(release_value MAX98357A_SD_CONTROL)"
SD_GPIO="$(release_value MAX98357A_SD_GPIO)"
SD_DELAY="$(release_value MAX98357A_SD_DELAY_MS)"
MCLK_FS="$(release_value MAX98357A_MCLK_FS)"
I2S_LRCLK="$(release_value I2S_LRCLK_GPIO)"
I2S_BCLK="$(release_value I2S_BCLK_GPIO)"
I2S_TX="$(release_value I2S_TX_GPIO)"
I2S_RX="$(release_value I2S_RX_GPIO)"
I2S_RX_PIN="$(release_value I2S_RX_HEADER_PIN)"
TOUCH_GPIO="$(release_value TOUCH_GPIO)"
TOUCH_PIN="$(release_value TOUCH_HEADER_PIN)"
LEGACY_SPDIF="$(release_value LEGACY_SPDIF_CODEC)"
LEGACY_AMP_GATE="$(release_value LEGACY_AMP_GATE)"
LEGACY_AMP_GATE_FILES="$(release_value LEGACY_AMP_GATE_FILES)"
JOURNAL_STORAGE="$(release_value JOURNAL_STORAGE)"
JOURNAL_RUNTIME_MAX_USE="$(release_value JOURNAL_RUNTIME_MAX_USE)"
TMP_MOUNT="$(release_value TMP_MOUNT)"
RUNTIME_WATCHDOG="$(release_value SYSTEMD_RUNTIME_WATCHDOG_SEC)"
REBOOT_WATCHDOG="$(release_value SYSTEMD_REBOOT_WATCHDOG_SEC)"
FIELD_DIAGNOSTICS="$(release_value FIELD_DIAGNOSTICS)"

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

if [ "$AUDIO_MODE" = playback-only ] && [ "$AUDIO_CAPTURE" = removed ] && \
   [ "$AUDIO_DRIVER" = "$EXPECTED_AUDIO_DRIVER" ] && \
   [ "$AUDIO_COMPATIBLE" = "$EXPECTED_AUDIO_COMPATIBLE" ] && \
   [ "$AUDIO_SOURCE" = "$EXPECTED_AUDIO_SOURCE" ] && \
   is_sha256 "$MODULE_SHA_META" && is_sha256 "$DTB_SHA_META" && \
   [ "$SD_CONTROL" = "$EXPECTED_SD_CONTROL" ] && \
   [ "$SD_GPIO" = "$EXPECTED_SD_GPIO" ] && \
   [ "$SD_DELAY" = "$EXPECTED_SD_DELAY_MS" ] && \
   [ "$MCLK_FS" = "$EXPECTED_MCLK_FS" ] && \
   [ "$I2S_LRCLK" = PA18 ] && [ "$I2S_BCLK" = PA19 ] && [ "$I2S_TX" = PA20 ] && \
   [ "$I2S_RX" = unassigned ] && [ "$I2S_RX_PIN" = 38-free ] && \
   [ "$TOUCH_GPIO" = PA17 ] && [ "$TOUCH_PIN" = 37 ] && \
   [ "$LEGACY_SPDIF" = removed ] && [ "$LEGACY_AMP_GATE" = removed ] && \
   [ "$LEGACY_AMP_GATE_FILES" = absent-verified ]; then
    ok "hardware-validated playback-only MAX98357A metadata"
else
    missing "hardware-validated playback-only MAX98357A metadata"
fi

if [ "$JOURNAL_STORAGE" = "$EXPECTED_JOURNAL_STORAGE" ] && \
   [ "$JOURNAL_RUNTIME_MAX_USE" = "$EXPECTED_JOURNAL_RUNTIME_MAX_USE" ] && \
   [ "$TMP_MOUNT" = "$EXPECTED_TMP_MOUNT" ] && \
   [ "$RUNTIME_WATCHDOG" = "$EXPECTED_RUNTIME_WATCHDOG" ] && \
   [ "$REBOOT_WATCHDOG" = "$EXPECTED_REBOOT_WATCHDOG" ] && \
   grep -qx 'Storage=volatile' /etc/systemd/journald.conf.d/20-mk-clock-volatile.conf 2>/dev/null && \
   grep -qx 'RuntimeMaxUse=16M' /etc/systemd/journald.conf.d/20-mk-clock-volatile.conf 2>/dev/null && \
   grep -qx 'RuntimeWatchdogSec=16s' /etc/systemd/system.conf.d/20-mk-clock-watchdog.conf 2>/dev/null && \
   grep -qx 'RebootWatchdogSec=16s' /etc/systemd/system.conf.d/20-mk-clock-watchdog.conf 2>/dev/null; then
    ok "volatile journal and hardware watchdog policy"
else
    missing "volatile journal / hardware watchdog policy"
fi

if [ "$FIELD_DIAGNOSTICS" = bpi-zero-diag ] && command -v bpi-zero-diag >/dev/null 2>&1; then
    ok "field diagnostics available"
else
    missing "field diagnostics"
fi

if [ -r "$FIRSTBOOT_STATUS" ] && \
   grep -qx 'FIRSTBOOT=complete' "$FIRSTBOOT_STATUS" 2>/dev/null && \
   grep -qx 'WIFI=connected' "$FIRSTBOOT_STATUS" 2>/dev/null; then
    ok "firstboot complete with Wi-Fi connected"
else
    missing "firstboot completion"
    [ -r "$FIRSTBOOT_STATUS" ] && while IFS= read -r line; do info "firstboot: $line"; done < "$FIRSTBOOT_STATUS"
fi

if command -v findmnt >/dev/null 2>&1 && [ "$(findmnt -n -o FSTYPE /tmp 2>/dev/null || true)" = tmpfs ]; then
    ok "/tmp is tmpfs"
else
    missing "/tmp tmpfs mount"
fi

if [ -e /dev/watchdog0 ] || [ -e /dev/watchdog ]; then
    ok "hardware watchdog device present"
else
    missing "hardware watchdog device"
fi

ACTIVE_MARKER=
if [ -r "$MARKER" ]; then
    ACTIVE_MARKER="$(tr -d '\0' < "$MARKER" 2>/dev/null || true)"
elif [ -r "$COMPAT_MARKER" ]; then
    ACTIVE_MARKER="$(tr -d '\0' < "$COMPAT_MARKER" 2>/dev/null || true)"
fi
if [ "$ACTIVE_MARKER" = "$EXPECTED_HARDWARE" ]; then
    ok "clock Device Tree active ($ACTIVE_MARKER)"
else
    missing "clock Device Tree marker"
    info "active: ${ACTIVE_MARKER:-none}"
fi

if [ "$ACTIVE_MARKER" = "$EXPECTED_HARDWARE" ] && [ ! -e /dev/spidev0.0 ]; then
    if command -v systemctl >/dev/null 2>&1 && systemctl cat mk-piclock-spidev.service >/dev/null 2>&1; then
        systemctl restart mk-piclock-spidev.service >/dev/null 2>&1 || true
    elif [ -x /usr/local/sbin/mk-piclock-bind-spidev ]; then
        /usr/local/sbin/mk-piclock-bind-spidev >/dev/null 2>&1 || true
    fi
fi
for dev in /dev/spidev0.0 /dev/gpiochip0 /dev/i2c-0; do
    if [ -e "$dev" ]; then ok "$dev"; else missing "$dev"; fi
done

if command -v modinfo >/dev/null 2>&1; then
    MAX_PATH="$(modinfo -n snd-soc-max98357a 2>/dev/null || true)"
    MAX_VM="$(modinfo -F vermagic snd-soc-max98357a 2>/dev/null | awk '{print $1}')"
    MAX_SHA="$(sha256sum "$MODULE_PATH" 2>/dev/null | awk '{print $1}')"
    if [ "$MAX_PATH" = "$MODULE_PATH" ] && [ "$MAX_VM" = "$EXPECTED_KERNEL" ] && \
       is_sha256 "$MODULE_SHA_META" && [ "$MAX_SHA" = "$MODULE_SHA_META" ]; then
        ok "release-pinned MAX98357A module"
    else
        missing "validated MAX98357A module"
        info "release path: ${MODULE_PATH:-none}"
        info "active path:  ${MAX_PATH:-none}"
        info "vermagic:     ${MAX_VM:-none}"
        info "sha256:       ${MAX_SHA:-none}"
    fi
    for mod in sun4i-i2s snd-soc-simple-card; do
        VM="$(modinfo -F vermagic "$mod" 2>/dev/null | awk '{print $1}')"
        [ "$VM" = "$EXPECTED_KERNEL" ] && ok "$mod vermagic" || missing "$mod vermagic"
    done
else
    missing "modinfo utility"
fi

for mod in snd_soc_max98357a sun4i_i2s snd_soc_simple_card; do
    if grep -q "^${mod} " /proc/modules 2>/dev/null; then ok "$mod loaded"; else missing "$mod loaded"; fi
done
if grep -q '^snd_soc_dmic ' /proc/modules 2>/dev/null; then missing "snd_soc_dmic absent"; else ok "snd_soc_dmic absent"; fi
if grep -q '^snd_soc_spdif_tx ' /proc/modules 2>/dev/null; then missing "legacy snd_soc_spdif_tx absent"; else ok "legacy snd_soc_spdif_tx absent"; fi

LIVE_CODEC="$(tr -d '\0' < /proc/device-tree/max98357a/compatible 2>/dev/null || true)"
[ "$LIVE_CODEC" = "$EXPECTED_AUDIO_COMPATIBLE" ] && ok "live codec $LIVE_CODEC" || missing "live MAX98357A codec"
if [ -e /proc/device-tree/dmic-codec ]; then missing "capture codec node absent"; else ok "capture codec node absent"; fi
if [ -e /proc/device-tree/sound-max98357a/icubedev,capture-rate-hz ]; then missing "capture-rate DT policy absent"; else ok "capture-rate DT policy absent"; fi
LIVE_SD_DELAY="$(property_hex /proc/device-tree/max98357a/sdmode-delay 2>/dev/null || true)"
[ "$LIVE_SD_DELAY" = 00000005 ] && ok "MAX98357A sdmode-delay 5 ms" || missing "MAX98357A sdmode-delay"
LIVE_MCLK_FS="$(property_hex /proc/device-tree/sound-max98357a/simple-audio-card,mclk-fs 2>/dev/null || true)"
[ "$LIVE_MCLK_FS" = 00000100 ] && ok "MAX98357A mclk-fs 256" || missing "MAX98357A mclk-fs"

if grep -Raq 'linux,spdif-dit' /proc/device-tree 2>/dev/null; then missing "legacy linux,spdif-dit absent from live Device Tree"; else ok "legacy linux,spdif-dit absent from live Device Tree"; fi

I2S_PINS=""
if [ -r /proc/device-tree/soc/pinctrl@1c20800/mk-piclock-i2s0-pins/pins ]; then
    I2S_PINS="$(tr '\0' ' ' < /proc/device-tree/soc/pinctrl@1c20800/mk-piclock-i2s0-pins/pins 2>/dev/null || true)"
fi
I2S_PINS_OK=1
for pin in PA18 PA19 PA20; do
    case " $I2S_PINS " in *" $pin "*) ;; *) I2S_PINS_OK=0 ;; esac
done
case " $I2S_PINS " in *' PA21 '*) I2S_PINS_OK=0 ;; esac
if [ "$I2S_PINS_OK" -eq 1 ]; then
    ok "I2S pin group is playback-only PA18/PA19/PA20"
else
    missing "playback-only I2S pin group"
    info "active: ${I2S_PINS:-none}"
fi

DTB_PATH=""
if [ -n "$CLOCK_DTB" ] && [ -n "$KERNEL_ABI" ]; then
    for candidate in \
        "/usr/lib/firmware/$KERNEL_ABI/device-tree/$CLOCK_DTB" \
        "/usr/lib/linux-image-$KERNEL_ABI/$CLOCK_DTB"; do
        if [ -r "$candidate" ]; then DTB_PATH="$candidate"; break; fi
    done
fi
DTB_SHA=""
[ -n "$DTB_PATH" ] && DTB_SHA="$(sha256sum "$DTB_PATH" 2>/dev/null | awk '{print $1}')"
if [ -n "$DTB_PATH" ] && is_sha256 "$DTB_SHA_META" && [ "$DTB_SHA" = "$DTB_SHA_META" ]; then
    ok "release-pinned clock DTB SHA256"
else
    missing "release-pinned clock DTB SHA256"
    info "release sha: ${DTB_SHA_META:-none}"
    info "active sha:  ${DTB_SHA:-none}"
fi

if [ -r /proc/asound/cards ] && grep -q 'MAX98357A' /proc/asound/cards; then ok "MAX98357A ALSA card"; else missing "MAX98357A ALSA card"; fi
if [ -r /proc/asound/pcm ] && grep -Eq '1c22000\.i2s.*playback[[:space:]]+1' /proc/asound/pcm; then ok "MAX98357A playback PCM path"; else missing "MAX98357A playback PCM path"; fi
if [ -r /proc/asound/pcm ] && grep -Eq '1c22000\.i2s.*capture[[:space:]]+1' /proc/asound/pcm; then missing "capture PCM absent"; else ok "capture PCM absent"; fi

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
    if [ -e "$path" ] || [ -L "$path" ]; then LEGACY_GATE_PATHS="${LEGACY_GATE_PATHS}${LEGACY_GATE_PATHS:+ }$path"; fi
done
if [ -n "$LEGACY_GATE_PATHS" ]; then missing "legacy mk-clock-amp-gate files absent"; else ok "legacy mk-clock-amp-gate files absent"; fi

if [ "$FAIL" -ne 0 ]; then
    echo
    echo 'Hardware diagnostics:'
    [ -r "$RELEASE" ] && { echo; echo '[/etc/bpi-zero-clock-release]'; cat "$RELEASE"; }
    echo; echo '[/proc/asound/cards]'; cat /proc/asound/cards 2>/dev/null || echo 'unavailable'
    echo; echo '[/proc/asound/pcm]'; cat /proc/asound/pcm 2>/dev/null || echo 'unavailable'
    echo; echo '[audio modules]'; grep -E 'snd_soc_(max98357a|dmic|simple_card|spdif_tx)|sun4i_i2s' /proc/modules 2>/dev/null || true
    if command -v bpi-zero-diag >/dev/null 2>&1; then echo; echo '[bpi-zero-diag]'; bpi-zero-diag 2>&1 || true; fi
    echo
    echo 'ERROR: the bpi-zero-clock playback hardware layer is not ready.'
    echo 'Flash/boot bpi-zero-clock 1.0.4-preview36 and allow first boot to complete before installing mk-clock-adult.'
    exit 1
fi

echo
echo 'All required bpi-zero-clock 1.0.4-preview36 playback hardware is ready.'
exit 0
