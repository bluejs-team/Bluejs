#!/usr/bin/env bash
# Build the Blue Windows installer (.exe) using NSIS.
#
# Usage (from repo root):
#   bash build/make-windows-installer.sh
#
# Requires:
#   makensis   (sudo apt install nsis)
#   The Windows zip must already be built:
#     bash examples/bluejs-landing/scripts/package-windows-release.sh
#
# Produces:
#   examples/bluejs-landing/public/downloads/blue-windows-x86_64-setup.exe

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(grep '^VERSION' "${ROOT}/Makefile" | head -1 | sed 's/.*:=\s*//' | tr -d '[:space:]')"
if [[ -z "$VERSION" ]]; then
  echo "Could not read VERSION from Makefile" >&2; exit 1
fi

OUTDIR="${ROOT}/dist"
OUT_EXE="${OUTDIR}/blue-windows-x86_64-setup.exe"
WIN_ZIP="${OUTDIR}/blue-windows-x86_64.zip"

if [[ ! -f "$WIN_ZIP" ]]; then
  echo "Missing $WIN_ZIP - run package-windows-release.sh first." >&2; exit 1
fi

if ! command -v makensis >/dev/null 2>&1; then
  echo "makensis not found. Install with: sudo apt install nsis" >&2; exit 1
fi

# ── Extract zip into a staging dir ────────────────────────────────────────────
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/blue-nsis.XXXXXX")"
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

unzip -q "$WIN_ZIP" -d "$STAGE"

# ── Generate NSIS script from template ────────────────────────────────────────
# Write NSI outside STAGE so "File /r @SRCDIR@\*.*" doesn't bundle it.
NSI_TEMPLATE="${ROOT}/build/blue-installer.nsi"
NSI_OUT="${STAGE}/../blue-installer-gen.nsi"

# Resolve icon - prefer the project logo, then existing .ico fallback.
ICON="${STAGE}/icon.ico"
if [[ -f "${ROOT}/logo.png" ]] && command -v convert >/dev/null 2>&1; then
  convert "${ROOT}/logo.png" -define icon:auto-resize=256,128,64,48,32,16 "$ICON" 2>/dev/null || true
fi
if [[ ! -f "$ICON" ]]; then
  ICON="${ROOT}/build/blue.ico"
fi

# LICENSE file
LICENSE="${ROOT}/LICENSE"
if [[ ! -f "$LICENSE" ]]; then
  echo "Bluejs JavaScript Compiler - Copyright Bluejs" > "${STAGE}/LICENSE.txt"
  LICENSE="${STAGE}/LICENSE.txt"
fi

sed \
  -e "s|@VERSION@|${VERSION}|g" \
  -e "s|@OUTFILE@|${OUT_EXE}|g" \
  -e "s|@SRCDIR@|${STAGE}|g" \
  -e "s|@ICON@|${ICON}|g" \
  -e "s|@LICENSE@|${LICENSE}|g" \
  "$NSI_TEMPLATE" > "$NSI_OUT"

# If icon doesn't exist, strip the icon lines so makensis doesn't error
if [[ ! -f "$ICON" ]]; then
  sed -i '/!define MUI_.*ICON/d' "$NSI_OUT"
fi

# ── EnVar plugin (for PATH modification) ──────────────────────────────────────
# Use bundled plugins from build/nsis-plugins/ - makensis -NOCD searches them
LOCAL_PLUGINS="${ROOT}/build/nsis-plugins"
ENVAR_FLAG=""
if [[ -d "$LOCAL_PLUGINS" ]]; then
  ENVAR_FLAG="-DNSISDIR=${LOCAL_PLUGINS}"
fi

# ── Build ──────────────────────────────────────────────────────────────────────
mkdir -p "$OUTDIR"
rm -f "$OUT_EXE"
echo "Building installer with makensis …"
makensis -V2 "-X!addplugindir \"${LOCAL_PLUGINS}/x86-unicode\"" "$NSI_OUT"

echo "Wrote ${OUT_EXE}"
ls -lh "$OUT_EXE"

trap - EXIT
cleanup
echo "Done."
