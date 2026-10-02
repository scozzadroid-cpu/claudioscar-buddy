#!/usr/bin/env bash
# Build every board, package the binaries and publish a GitHub release that
# the buddies pick up over the air.
#
#   tools/release.sh 1.0.3 "Short release title" notes.md
#
# Needs: PlatformIO (pio), esptool (python -m esptool), GitHub CLI (gh, logged in).
set -euo pipefail

VER="${1:?version, e.g. 1.0.3}"
TITLE="${2:-claudioscar-buddy v$VER}"
NOTES="${3:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PIO="${PIO:-pio}"
PY="${PYTHON:-python}"
OUT="dist/v$VER"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

# env name : short tag used in the full-image file name
BOARDS=(
  "waveshare-esp32s3-touch-amoled-1-8:v1"
  "waveshare-esp32s3-touch-amoled-1-8-v2:v2"
)

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "Working tree not clean - commit first." >&2; exit 1
fi

sed -i "s/^custom_fw_version = .*/custom_fw_version = $VER/" platformio.ini
git add platformio.ini
git commit -q -m "Release v$VER" || true

rm -rf "$OUT"; mkdir -p "$OUT"
for b in "${BOARDS[@]}"; do
  env="${b%%:*}"; tag="${b##*:}"
  "$PIO" run -e "$env"
  bd=".pio/build/$env"
  # Full image for a first USB install (offset 0x0).
  "$PY" -m esptool --chip esp32s3 merge-bin -o "$OUT/claudioscar-buddy-v$VER-amoled-1.8-$tag.bin" \
    --flash-mode qio --flash-size 8MB \
    0x0 "$bd/bootloader.bin" 0x8000 "$bd/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$bd/firmware.bin"
  # App-only image the buddy downloads over the air. The name must match
  # OTA_ASSET in platformio.ini.
  cp "$bd/firmware.bin" "$OUT/claudioscar-buddy-ota-$env.bin"
done
(cd "$OUT" && sha256sum *.bin > SHA256SUMS.txt)

git push -q origin HEAD
git tag -f "v$VER" HEAD
git push -q -f origin "v$VER"

args=(--title "$TITLE")
if [ -n "$NOTES" ]; then args+=(--notes-file "$NOTES"); else args+=(--generate-notes); fi
gh release create "v$VER" "${args[@]}" "$OUT"/*.bin "$OUT/SHA256SUMS.txt"
echo "Published v$VER - buddies on WiFi will offer it under Updates."
