#!/usr/bin/env bash
# Build installable Debian package for public/downloads/
#
# Run from the repo root:
#   bash examples/bluejs-landing/scripts/package-debian.sh
#
# Requires: make && make deps && make tools-deps

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="${ROOT}/dist"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/blue-deb.XXXXXX")"

cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

# ── Preflight ─────────────────────────────────────────────────────────────────
for f in blue_bin blue vendor/js/esprima.js vendor/js/babel.min.js; do
  if [[ ! -e "${ROOT}/${f}" ]]; then
    echo "Missing ${ROOT}/${f} - run 'make' and 'make deps' from repo root first." >&2
    exit 1
  fi
done

if [[ ! -f "${ROOT}/tools/jsc-npm-bundle/bundle.mjs" ]]; then
  echo "Missing tools/jsc-npm-bundle - run 'make tools-deps' from repo root." >&2
  exit 1
fi

if ! command -v dpkg-deb >/dev/null 2>&1; then
  echo "dpkg-deb not found - install dpkg on this machine to build .deb packages." >&2
  exit 1
fi

VERSION="$(grep '^VERSION' "${ROOT}/Makefile" | head -1 | sed 's/.*:=\s*//' | tr -d '[:space:]')"
if [[ -z "$VERSION" ]]; then
  echo "Could not read VERSION from ${ROOT}/Makefile" >&2
  exit 1
fi
echo "Building Blue ${VERSION} .deb …"

# ── Architecture ──────────────────────────────────────────────────────────────
ARCH="$(uname -m)"
case "$ARCH" in
  x86_64)          DEB_ARCH="amd64" ;;
  aarch64 | arm64) DEB_ARCH="arm64" ;;
  *)               DEB_ARCH="$ARCH" ;;
esac

# ── Stage filesystem ──────────────────────────────────────────────────────────
mkdir -p "${STAGE}/usr/lib/blue/tools"
mkdir -p "${STAGE}/usr/bin"
mkdir -p "${STAGE}/DEBIAN"

mkdir -p "${STAGE}/usr/lib/blue/src" "${STAGE}/usr/lib/blue/vendor/quickjs/obj"

# Compiler binary + JS dependencies
cp -a "${ROOT}/blue_bin" "${ROOT}/blue" "${STAGE}/usr/lib/blue/"
cp -a "${ROOT}/vendor/js/esprima.js" "${ROOT}/vendor/js/babel.min.js" "${STAGE}/usr/lib/blue/"
cp -a "${ROOT}/tools/jsc-npm-bundle" "${STAGE}/usr/lib/blue/tools/"

# Runtime headers (only what emitted C++ #includes - not compiler internals)
for f in js_value.h js_value.hpp js_node.h js_globals.h js_window.h; do
  [ -f "${ROOT}/src/${f}" ] && cp "${ROOT}/src/${f}" "${STAGE}/usr/lib/blue/src/"
done

# Runtime sources + headers + precompiled archives.
# The compiler driver compiles runtime/*.cpp at user build time, so these
# must ship even though they look like "source code" - they're the runtime
# implementation, not compiler internals.
cp -a "${ROOT}/src/runtime" "${STAGE}/usr/lib/blue/src/"

# QuickJS headers + precompiled objects (no .c sources)
cp "${ROOT}"/vendor/quickjs/*.h "${STAGE}/usr/lib/blue/vendor/quickjs/"
[ -d "${ROOT}/vendor/quickjs/obj" ] && cp "${ROOT}"/vendor/quickjs/obj/*.o "${STAGE}/usr/lib/blue/vendor/quickjs/obj/"

# Wrapper script at /usr/bin/blue
cat << 'WRAPPER' > "${STAGE}/usr/bin/blue"
#!/usr/bin/env bash
exec /usr/lib/blue/blue "$@"
WRAPPER
chmod +x "${STAGE}/usr/bin/blue"

# Ensure esbuild is bundled
if [[ ! -d "${STAGE}/usr/lib/blue/tools/jsc-npm-bundle/node_modules" ]]; then
  echo "Installing esbuild under tools/jsc-npm-bundle …"
  (cd "${STAGE}/usr/lib/blue/tools/jsc-npm-bundle" && npm install --omit=dev --no-audit --no-fund)
fi

if [[ ! -f "${STAGE}/usr/lib/blue/tools/jsc-npm-bundle/node_modules/esbuild/package.json" ]]; then
  echo "esbuild still missing after npm install." >&2
  exit 1
fi

# ── DEBIAN/control ────────────────────────────────────────────────────────────
SIZE=$(du -ks "$STAGE/usr" 2>/dev/null | cut -f1)
SIZE="${SIZE:-0}"
cat << EOF > "${STAGE}/DEBIAN/control"
Package: blue
Version: ${VERSION}
Section: devel
Priority: optional
Architecture: ${DEB_ARCH}
Installed-Size: ${SIZE}
Maintainer: Blue Team <team@bluejs.dev>
Depends: build-essential, pkg-config
Recommends: libuv1-dev, libgtk-3-dev, libwebkit2gtk-4.1-dev | libwebkit2gtk-4.0-dev, nodejs
Description: Blue - Compile JavaScript to small native apps.
 Blue is an ahead-of-time compiler plus a tightly integrated hybrid runtime.
 .
 Required to build AOT apps: build-essential, pkg-config
 Required for HTTP / hybrid apps: libuv1-dev, nodejs
 Required for WebView apps: libgtk-3-dev, libwebkit2gtk-4.1-dev
EOF

# ── DEBIAN/postinst ───────────────────────────────────────────────────────────
cat << 'POSTINST' > "${STAGE}/DEBIAN/postinst"
#!/bin/sh
set -e

MISSING=""
MISSING_PKGS=""

# C++ compiler (hard requirement - Blue emits C++ and compiles it)
if ! command -v g++ >/dev/null 2>&1 && ! command -v c++ >/dev/null 2>&1; then
    MISSING="${MISSING}  - g++ (C++ compiler, required for all builds)\n"
    MISSING_PKGS="build-essential ${MISSING_PKGS}"
fi

# pkg-config (hard requirement - locates libuv / GTK / WebKit at link time)
if ! command -v pkg-config >/dev/null 2>&1; then
    MISSING="${MISSING}  - pkg-config (required for all builds)\n"
    MISSING_PKGS="pkg-config ${MISSING_PKGS}"
fi

# Node.js (needed for hybrid -build / esbuild pipeline)
if ! command -v node >/dev/null 2>&1; then
    MISSING="${MISSING}  - nodejs (needed for hybrid and React builds)\n"
    MISSING_PKGS="nodejs ${MISSING_PKGS}"
fi

# libuv (needed for HTTP server and hybrid island)
if ! dpkg -s libuv1-dev >/dev/null 2>&1; then
    MISSING="${MISSING}  - libuv1-dev (needed for HTTP server / hybrid island)\n"
    MISSING_PKGS="libuv1-dev ${MISSING_PKGS}"
fi

# GTK 3 (needed for native window / WebView)
if ! dpkg -s libgtk-3-dev >/dev/null 2>&1; then
    MISSING="${MISSING}  - libgtk-3-dev (needed for WebView window apps)\n"
    MISSING_PKGS="libgtk-3-dev ${MISSING_PKGS}"
fi

# WebKit2GTK (needed for WebView)
if ! dpkg -s libwebkit2gtk-4.1-dev >/dev/null 2>&1 && \
   ! dpkg -s libwebkit2gtk-4.0-dev >/dev/null 2>&1; then
    MISSING="${MISSING}  - libwebkit2gtk-4.1-dev (needed for WebView window apps)\n"
    MISSING_PKGS="libwebkit2gtk-4.1-dev ${MISSING_PKGS}"
fi

if [ -n "$MISSING" ]; then
    printf '\n'
    printf '==========================================================\n'
    printf 'Blue installed, but some dependencies are missing:\n\n'
    printf '%b' "$MISSING"
    printf '\nInstall them with:\n'
    [[ -n "$MISSING_PKGS" ]] && printf '  sudo apt update && sudo apt install -y %s\n' "$MISSING_PKGS"
    printf '==========================================================\n\n'
fi

chmod +x /usr/lib/blue/blue /usr/lib/blue/blue_bin

exit 0
POSTINST
chmod 755 "${STAGE}/DEBIAN/postinst"

# ── File permissions ──────────────────────────────────────────────────────────
find "$STAGE" -type d -exec chmod 755 {} +
find "$STAGE" -type f ! -path "*/DEBIAN/*" -exec chmod 644 {} +

chmod +x "${STAGE}/usr/lib/blue/blue"
chmod +x "${STAGE}/usr/lib/blue/blue_bin"
chmod +x "${STAGE}/usr/bin/blue"

if [[ -d "${STAGE}/usr/lib/blue/tools/jsc-npm-bundle/node_modules" ]]; then
  find "${STAGE}/usr/lib/blue/tools/jsc-npm-bundle/node_modules" \
    -type f \( -name "esbuild" -o -path "*/bin/*" \) -exec chmod +x {} +
fi

# ── Build package ─────────────────────────────────────────────────────────────
mkdir -p "${OUTDIR}"
OUT_NAME="blue-linux-${DEB_ARCH}.deb"

echo "Building ${OUT_NAME} …"
dpkg-deb --root-owner-group --build "${STAGE}" "${OUTDIR}/${OUT_NAME}"
echo "Wrote ${OUTDIR}/${OUT_NAME}"
ls -lh "${OUTDIR}/${OUT_NAME}"

trap - EXIT
cleanup

echo "Done."
