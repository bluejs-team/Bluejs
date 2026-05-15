#!/usr/bin/env bash
# Cross-compile and package blue for Windows (x86_64).
#
# Usage (from repo root):
#   bash examples/bluejs-landing/scripts/package-windows-release.sh
#
# Requires:
#   make windows  (cross-compiles blue_bin.exe via MinGW)
#   make deps     (esprima.js, babel.min.js, nlohmann/json)
#   make tools-deps (esbuild, for hybrid builds)
#
# Produces:
#   examples/bluejs-landing/public/downloads/blue-windows-x86_64.zip

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(grep '^VERSION' "${ROOT}/Makefile" | head -1 | sed 's/.*:=\s*//' | tr -d '[:space:]')"
if [[ -z "$VERSION" ]]; then
  echo "Could not read VERSION from ${ROOT}/Makefile" >&2
  exit 1
fi
echo "Packaging Blue ${VERSION} for Windows …"

OUTDIR="${ROOT}/dist"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/blue-win-pkg.XXXXXX")"
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

# ── Preflight ─────────────────────────────────────────────────────────────────
if [[ ! -f "${ROOT}/blue.exe" ]]; then
  echo "Missing ${ROOT}/blue.exe - run 'make windows' first." >&2
  exit 1
fi
for f in vendor/js/esprima.js vendor/js/babel.min.js install.ps1; do
  if [[ ! -f "${ROOT}/${f}" ]]; then
    echo "Missing ${ROOT}/${f} - run 'make deps' from repo root first." >&2
    exit 1
  fi
done
if [[ ! -f "${ROOT}/tools/jsc-npm-bundle/bundle.mjs" ]]; then
  echo "Missing tools/jsc-npm-bundle - run 'make tools-deps' from repo root." >&2
  exit 1
fi
if ! command -v zip >/dev/null 2>&1; then
  echo "zip not found - sudo apt install zip" >&2
  exit 1
fi

# ── Stage files ───────────────────────────────────────────────────────────────
mkdir -p "${STAGE}/tools" "${STAGE}/src" "${STAGE}/vendor/quickjs/obj"

# Compiler binary + JS dependencies
cp "${ROOT}/blue.exe"               "${STAGE}/"
cp "${ROOT}/install.ps1"            "${STAGE}/"
cp "${ROOT}/vendor/js/esprima.js"   "${STAGE}/"
cp "${ROOT}/vendor/js/babel.min.js" "${STAGE}/"
cp -a "${ROOT}/tools/jsc-npm-bundle" "${STAGE}/tools/"

# Runtime headers (only what emitted C++ #includes - not compiler internals)
for f in js_value.h js_value.hpp js_node.h js_globals.h js_window.h; do
  [ -f "${ROOT}/src/${f}" ] && cp "${ROOT}/src/${f}" "${STAGE}/src/"
done

# Runtime sources + headers + precompiled archives.
# The compiler driver compiles runtime/*.cpp at user build time, so these
# must ship even though they look like "source code" - they're the runtime
# implementation, not compiler internals.
cp -a "${ROOT}/src/runtime" "${STAGE}/src/"

# QuickJS headers + precompiled Windows objects (no .c sources)
cp "${ROOT}"/vendor/quickjs/*.h "${STAGE}/vendor/quickjs/"
[ -d "${ROOT}/vendor/quickjs/obj-win" ] && cp "${ROOT}"/vendor/quickjs/obj-win/*.o "${STAGE}/vendor/quickjs/obj/"

# WebView2 SDK - required to link Blue.Window / window.open on Windows
if [[ ! -d "${ROOT}/vendor/webview2/build/native/include" ]]; then
  echo "Missing vendor/webview2 - run 'make deps' to fetch it." >&2; exit 1
fi
cp -a "${ROOT}/vendor/webview2" "${STAGE}/vendor/"

# libuv (Windows) - required for HTTP / hybrid builds
if [[ ! -f "${ROOT}/vendor/libuv-win/lib/libuv_a.a" ]]; then
  echo "Missing vendor/libuv-win - run 'make deps' to build it." >&2; exit 1
fi
cp -a "${ROOT}/vendor/libuv-win" "${STAGE}/vendor/"

# ── Zip ───────────────────────────────────────────────────────────────────────
mkdir -p "${OUTDIR}"
OUT_NAME="blue-windows-x86_64.zip"
rm -f "${OUTDIR}/${OUT_NAME}"
(cd "$STAGE" && zip -qr "${OUTDIR}/${OUT_NAME}" .)

echo "Wrote ${OUTDIR}/${OUT_NAME}"
ls -lh "${OUTDIR}/${OUT_NAME}"

trap - EXIT
cleanup
echo "Done."
