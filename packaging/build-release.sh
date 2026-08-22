#!/bin/bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(tr -d '[:space:]' < "$ROOT/VERSION")
PRODUCT="mk-clock-adult-${VERSION}-bpi-m2-zero-r1"
OUTPUT=${1:-"$ROOT/../${PRODUCT}.zip"}
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1767225600} # 2026-01-01T00:00:00Z

GUI_PRODUCT=$(sed -n "s/^const GUI_VERSION = '\([^']*\)';/\1/p" "$ROOT/web/assets/js/app.js" | head -1)
HW_PRODUCT=$(sed -n 's/^#define MP_PRODUCT_VERSION "\([^"]*\)"/\1/p' "$ROOT/hardware_profile.h" | head -1)
[ "$GUI_PRODUCT" = "$PRODUCT" ] || { echo "ERROR: GUI product identity '$GUI_PRODUCT' != '$PRODUCT'." >&2; exit 1; }
[ "$HW_PRODUCT" = "$PRODUCT" ] || { echo "ERROR: hardware product identity '$HW_PRODUCT' != '$PRODUCT'." >&2; exit 1; }
STALE_WEB=$(grep -RIl --include='*.html' --include='*.js' 'mk-clock-adult-[0-9][0-9.]*-bpi-m2-zero-r1' "$ROOT/web" | while IFS= read -r f; do grep -Eo 'mk-clock-adult-[0-9]+(\.[0-9]+)+-bpi-m2-zero-r1' "$f"; done | sort -u | grep -vxF "$PRODUCT" || true)
[ -z "$STALE_WEB" ] || { echo "ERROR: stale web product identity reference(s): $STALE_WEB" >&2; exit 1; }

case "$SOURCE_DATE_EPOCH" in
    ''|*[!0-9]*) echo "ERROR: SOURCE_DATE_EPOCH must be an integer epoch." >&2; exit 1 ;;
esac

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
STAGE="$TMP/$PRODUCT"
mkdir -p "$STAGE"

# Copy source release content only. Build outputs are regenerated on the target.
cp -a "$ROOT/." "$STAGE/"
rm -f "$STAGE/mk-piclock-core" "$STAGE/mk-piclock-api"
rm -rf "$STAGE/weather/build"

# ZIP timestamps are timezone-less. Normalize all entries to a fixed historical
# instant and create the archive under UTC so extraction cannot make source files
# appear to be in the future on Mountain-time clocks.
find "$STAGE" -exec touch -h -d "@${SOURCE_DATE_EPOCH}" {} +
mkdir -p "$(dirname "$OUTPUT")"
rm -f "$OUTPUT"
(
    cd "$TMP"
    export TZ=UTC
    find "$PRODUCT" -type f -print | LC_ALL=C sort | zip -X -q "$OUTPUT" -@
)

unzip -tq "$OUTPUT" >/dev/null
printf 'Created %s\n' "$OUTPUT"
sha256sum "$OUTPUT"
