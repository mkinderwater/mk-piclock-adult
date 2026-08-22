#!/bin/bash
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(tr -d '[:space:]' < "$ROOT_DIR/VERSION")
EXPECTED_PRODUCT="mk-clock-adult-${VERSION}-bpi-m2-zero-r1"
EXPECTED_API_VERSION=$(sed -n 's/^#define API_VERSION "\([^"]*\)"/\1/p' "$ROOT_DIR/mk-piclock-api.c" | head -1)

ok()   { printf 'OK      %s\n' "$1"; }
fail() { printf 'ERROR   %s\n' "$1" >&2; return 1; }

HTTP_PROBE_REASON=""
http_probe() {
    local response="" line="" normalized
    HTTP_PROBE_REASON="API connection to 127.0.0.1:8080 failed"
    if ! exec 3<>/dev/tcp/127.0.0.1/8080; then
        return 1
    fi
    if ! printf 'GET /api/v1/auth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n' >&3; then
        exec 3<&-
        exec 3>&-
        HTTP_PROBE_REASON="API health request could not be sent"
        return 1
    fi

    # Bash read(1) returns non-zero when EOF follows a final line without a newline.
    # Preserve that line because HTTP bodies are not required to end in LF.
    while :; do
        line=""
        if IFS= read -r -t 2 line <&3; then
            response+="$line"$'\n'
        else
            [ -n "$line" ] && response+="$line"$'\n'
            break
        fi
    done
    exec 3<&-
    exec 3>&-

    normalized=${response//$'\r'/}
    if ! grep -Eq '^HTTP/1\.[01] 200([[:space:]]|$)' <<<"$normalized"; then
        HTTP_PROBE_REASON="API health endpoint did not return HTTP 200"
        return 1
    fi
    if ! grep -Eiq "^X-MK-PICLOCK-API-Version:[[:space:]]*${EXPECTED_API_VERSION}[[:space:]]*$" <<<"$normalized"; then
        HTTP_PROBE_REASON="API response did not identify as v${EXPECTED_API_VERSION}"
        return 1
    fi
    if ! grep -Fq '"ok":true' <<<"$normalized"; then
        HTTP_PROBE_REASON="API health endpoint returned an invalid JSON status"
        return 1
    fi
    HTTP_PROBE_REASON=""
    return 0
}

wait_for() {
    local attempt
    for attempt in $(seq 1 15); do
        if "$@" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    return 1
}

wait_for systemctl is-active --quiet mk-piclock-core.service || fail 'mk-piclock-core.service is not active'
ok 'core service active'

wait_for test -S /run/mk-piclock/core.sock || fail 'core socket /run/mk-piclock/core.sock is unavailable'
ok 'core socket available'

wait_for systemctl is-active --quiet mk-piclock-api.service || fail 'mk-piclock-api.service is not active'
ok 'API service active'

if ! wait_for http_probe; then
    fail "${HTTP_PROBE_REASON:-API health check failed}"
fi
ok "API v${EXPECTED_API_VERSION} responding"

grep -qF "$EXPECTED_PRODUCT" /opt/mk-piclock/web/assets/js/app.js || fail "installed GUI does not identify as $EXPECTED_PRODUCT"
ok "application files identify as ${EXPECTED_PRODUCT}"

test -x /usr/local/libexec/mk-clock-system-helper || fail 'system configuration helper is missing or not executable'
test -r /etc/systemd/system/mk-clock-system-config.service || fail 'system configuration helper service is missing'
test -r /etc/polkit-1/rules.d/49-mk-clock-adult-system.rules || fail 'system configuration PolicyKit rule is missing'
ok 'system configuration helper installed'

systemctl is-active --quiet mk-piclock-weather.path || fail 'mk-piclock-weather.path is not active'
ok 'weather path active'

systemctl is-active --quiet mk-piclock-weather.timer || fail 'mk-piclock-weather.timer is not active'
ok 'weather timer active'
