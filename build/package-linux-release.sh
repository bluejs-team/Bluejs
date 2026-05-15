#!/usr/bin/env bash
# Build installable tarballs for public/downloads/ (run from compiler repo root or via path below).
#
# Usage (from repo root):
#   bash examples/bluejs-landing/scripts/package-linux-release.sh
#
# Requires: `make` has been run (blue_bin, blue, esprima.js, babel.min.js); `make tools-deps`
# for esbuild. Produces:
#   examples/bluejs-landing/public/downloads/blue-linux-x86_64.tar.gz
#   examples/bluejs-landing/public/downloads/blue-linux-aarch64.tar.gz  (only if cross-build available)
#
# For a single-arch deploy, ship the matching file for your server CPU.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(grep '^VERSION' "${ROOT}/Makefile" | head -1 | sed 's/.*:=\s*//' | tr -d '[:space:]')"
if [[ -z "$VERSION" ]]; then
  echo "Could not read VERSION from ${ROOT}/Makefile" >&2
  exit 1
fi
echo "Packaging Blue ${VERSION} tarball …"
OUTDIR="${ROOT}/dist"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/blue-pkg.XXXXXX")"

cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

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

mkdir -p "${STAGE}/tools" "${STAGE}/src" "${STAGE}/vendor/quickjs/obj"

# Compiler binary + JS dependencies
cp -a "${ROOT}/blue_bin" "${ROOT}/blue" "${STAGE}/"
cp -a "${ROOT}/vendor/js/esprima.js" "${ROOT}/vendor/js/babel.min.js" "${STAGE}/"
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

# QuickJS headers + precompiled objects (no .c sources)
cp "${ROOT}"/vendor/quickjs/*.h "${STAGE}/vendor/quickjs/"
[ -d "${ROOT}/vendor/quickjs/obj" ] && cp "${ROOT}"/vendor/quickjs/obj/*.o "${STAGE}/vendor/quickjs/obj/"

if [[ -d "${STAGE}/tools/jsc-npm-bundle/node_modules" ]]; then
  :
else
  echo "Installing esbuild under tools/jsc-npm-bundle …"
  (cd "${STAGE}/tools/jsc-npm-bundle" && npm install --omit=dev --no-audit --no-fund)
fi

if [[ ! -f "${STAGE}/tools/jsc-npm-bundle/node_modules/esbuild/package.json" ]]; then
  echo "esbuild still missing after npm install." >&2
  exit 1
fi

mkdir -p "${OUTDIR}"

ARCH="$(uname -m)"
case "$ARCH" in
  x86_64) OUT_NAME="blue-linux-x86_64.tar.gz" ;;
  aarch64 | arm64) OUT_NAME="blue-linux-aarch64.tar.gz" ;;
  *)
    echo "Packaging on $ARCH - naming tarball blue-linux-${ARCH}.tar.gz"
    OUT_NAME="blue-linux-${ARCH}.tar.gz"
    ;;
esac

tar -czf "${OUTDIR}/${OUT_NAME}" -C "$STAGE" . || { echo "Error: tar failed" >&2; exit 1; }
echo "Wrote ${OUTDIR}/${OUT_NAME}"
ls -lh "${OUTDIR}/${OUT_NAME}"

trap - EXIT
cleanup

echo ""
echo "If you need both x86_64 and aarch64 archives, run this script on each machine (or use cross-build later)."
echo "Then rebuild the landing binary so GET /downloads/blue-linux-*.tar.gz serves the new payloads."
