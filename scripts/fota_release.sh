#!/usr/bin/env bash
# fota_release.sh — Build AdhanAI firmware and upload a FOTA release to S3.
#
# Usage:
#   ./scripts/fota_release.sh              # uses default env (esp32dev) — PRODUCTION channel
#   ./scripts/fota_release.sh esp32dev-test  # TEST channel — isolated S3 prefix, only
#                                             # reachable by devices built with the
#                                             # esp32dev-test PlatformIO env
#   ./scripts/fota_release.sh native       # dry-run friendly (won't find a bin)
#
# Requires: pio, aws CLI (configured with access to S3_BUCKET), shasum/sha256sum, jq

set -euo pipefail

# ── Config ────────────────────────────────────────────────────────────────────
PIO_ENV="${1:-esp32dev}"
S3_BUCKET="my-adhan-firmware"
S3_REGION="eu-north-1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Helpers ───────────────────────────────────────────────────────────────────
die()  { echo "ERROR: $*" >&2; exit 1; }
info() { echo "  $*"; }

sha256_of() {
  if command -v sha256sum &>/dev/null; then
    sha256sum "$1" | awk '{print $1}'
  else
    shasum -a 256 "$1" | awk '{print $1}'
  fi
}

# ── Step 1: Extract version from Globals.cpp ─────────────────────────────────
echo ""
echo "┌─ Step 1: Resolving firmware version"
GLOBALS_CPP="$REPO_ROOT/src/Globals.cpp"
[[ -f "$GLOBALS_CPP" ]] || die "Globals.cpp not found at $GLOBALS_CPP"

FW_VERSION=$(grep -oE 'FW_VER\s*=\s*"[0-9]+\.[0-9]+\.[0-9]+"' "$GLOBALS_CPP" \
  | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
[[ -n "$FW_VERSION" ]] || die "Could not extract FW_VER from Globals.cpp"
info "Version : $FW_VERSION"
info "Env     : $PIO_ENV"

# ── Step 2: Build ─────────────────────────────────────────────────────────────
echo ""
echo "┌─ Step 2: Building firmware (pio run -e $PIO_ENV)"
cd "$REPO_ROOT"
pio run -e "$PIO_ENV"

# ── Step 3: Locate binary ─────────────────────────────────────────────────────
echo ""
echo "┌─ Step 3: Locating binary"
BIN_PATH="$REPO_ROOT/.pio/build/$PIO_ENV/firmware.bin"
[[ -f "$BIN_PATH" ]] || die "firmware.bin not found at $BIN_PATH"

BIN_SIZE=$(wc -c < "$BIN_PATH" | tr -d ' ')
BIN_SHA256=$(sha256_of "$BIN_PATH")

info "Binary  : $BIN_PATH"
info "Size    : $BIN_SIZE bytes"
info "SHA256  : $BIN_SHA256"

# ── Step 4: Derive S3 paths ───────────────────────────────────────────────────
echo ""
echo "┌─ Step 4: Planned S3 paths"
S3_KEY_BIN="$PIO_ENV/$FW_VERSION/firmware.bin"
S3_KEY_MANIFEST="$PIO_ENV/latest.json"
FIRMWARE_HTTPS_URL="https://$S3_BUCKET.s3.$S3_REGION.amazonaws.com/$S3_KEY_BIN"

info "Firmware : s3://$S3_BUCKET/$S3_KEY_BIN"
info "Manifest : s3://$S3_BUCKET/$S3_KEY_MANIFEST"
info "HTTPS URL: $FIRMWARE_HTTPS_URL"

# ── Step 5: Build manifest ────────────────────────────────────────────────────
echo ""
echo "┌─ Step 5: Building manifest (latest.json)"
MANIFEST_TMP=$(mktemp /tmp/adhan_latest_XXXXXX.json)
cat > "$MANIFEST_TMP" <<EOF
{
  "version": "$FW_VERSION",
  "url": "$FIRMWARE_HTTPS_URL",
  "size": $BIN_SIZE,
  "sha256": "$BIN_SHA256"
}
EOF
cat "$MANIFEST_TMP"

# ── Step 6: Summary + confirmation ───────────────────────────────────────────
echo ""
echo "┌─ Step 6: Release summary"
echo "│"
echo "│  Version : $FW_VERSION"
echo "│  Binary  : s3://$S3_BUCKET/$S3_KEY_BIN  ($BIN_SIZE bytes)"
echo "│  Manifest: s3://$S3_BUCKET/$S3_KEY_MANIFEST"
echo "│  SHA256  : $BIN_SHA256"
echo "│"
echo "│  Commands that will run:"
echo "│    aws s3 cp $BIN_PATH \\"
echo "│      s3://$S3_BUCKET/$S3_KEY_BIN \\"
echo "│      --content-type application/octet-stream \\"
echo "│      --region $S3_REGION"
echo "│"
echo "│    aws s3 cp $MANIFEST_TMP \\"
echo "│      s3://$S3_BUCKET/$S3_KEY_MANIFEST \\"
echo "│      --content-type application/json \\"
echo "│      --region $S3_REGION"
echo "│"
printf "└─ Deploy to FOTA? [y/N] "
read -r CONFIRM

if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
  echo "Aborted — nothing uploaded."
  rm -f "$MANIFEST_TMP"
  exit 0
fi

# ── Step 7: Upload ────────────────────────────────────────────────────────────
echo ""
echo "┌─ Step 7: Uploading to S3"

info "Uploading firmware.bin..."
aws s3 cp "$BIN_PATH" \
  "s3://$S3_BUCKET/$S3_KEY_BIN" \
  --content-type application/octet-stream \
  --region "$S3_REGION"

info "Uploading latest.json..."
aws s3 cp "$MANIFEST_TMP" \
  "s3://$S3_BUCKET/$S3_KEY_MANIFEST" \
  --content-type application/json \
  --region "$S3_REGION"

rm -f "$MANIFEST_TMP"

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
echo "✓ FOTA release $FW_VERSION uploaded successfully."
echo "  Firmware : https://$S3_BUCKET.s3.$S3_REGION.amazonaws.com/$S3_KEY_BIN"
echo "  Manifest : https://$S3_BUCKET.s3.$S3_REGION.amazonaws.com/$S3_KEY_MANIFEST"
