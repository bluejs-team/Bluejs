#!/usr/bin/env bash
# Full Blue release pipeline.
#
# Usage (from compiler repo root):
#   bash build/pipeline.sh
#
# Steps:
#   1. Precompile Linux runtime  → src/runtime/libblue_runtime.a
#   2. Precompile Windows runtime → src/runtime/libblue_runtime_win.a
#   3. Build Linux tar.gz, Windows zip, Debian .deb
#   4. Verify no source leaks in packages

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "══════════════════════════════════════════"
echo " Blue release pipeline"
echo "══════════════════════════════════════════"

# ── 1. Linux runtime .a ───────────────────────────────────────────────────────
echo ""
echo "── Phase 1: Precompile Linux runtime ──"
bash "${ROOT}/build/precompile-runtime.sh"

# ── 2. Windows runtime .a (MinGW cross-compile) ───────────────────────────────
echo ""
echo "── Phase 2: Precompile Windows runtime ──"
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  bash "${ROOT}/build/precompile-runtime.sh" --windows
else
  echo "  MinGW not found - installing …"
  sudo apt-get install -y -qq g++-mingw-w64-x86-64
  bash "${ROOT}/build/precompile-runtime.sh" --windows
fi

# ── 3. Packages ───────────────────────────────────────────────────────────────
echo ""
echo "── Phase 3: Building packages ──"
bash "${ROOT}/examples/bluejs-landing/scripts/package-linux-release.sh"
bash "${ROOT}/examples/bluejs-landing/scripts/package-windows-release.sh"
bash "${ROOT}/examples/bluejs-landing/scripts/package-debian.sh"

# ── 4. Verify ─────────────────────────────────────────────────────────────────
echo ""
echo "── Phase 4: Verifying package contents ──"

DOWNLOADS="${ROOT}/examples/bluejs-landing/public/downloads"
FAIL=0

check_no_leak() {
  local label="$1"
  # Files that must NOT appear - compiler internals and QJS implementation
  local leaked
  # Flag only actual compiler internals - not the intentionally-shipped headers/archives
  leaked=$(echo "${pkg_contents}" | grep -E \
    '\.(cpp|mm)$|main\.cpp|parser\.hpp|emitter\.hpp|babel_transform\.hpp|ast_node\.hpp|module_resolver\.hpp|jsc_bundle\.hpp|jsc_embed_generator\.hpp|jsc_npm_bundle\.hpp|quickjs_compat\.h|nlohmann/.*\.hpp' \
    || true)
  if [[ -n "${leaked}" ]]; then
    echo "  LEAK in ${label}:"
    echo "${leaked}"
    FAIL=1
  else
    echo "  ${label}: clean ✓"
  fi
}

pkg_contents=$(tar -tzf "${DOWNLOADS}/blue-linux-x86_64.tar.gz" 2>/dev/null)
check_no_leak "linux tar.gz"

pkg_contents=$(unzip -l "${DOWNLOADS}/blue-windows-x86_64.zip" 2>/dev/null | awk '{print $4}')
check_no_leak "windows zip"

pkg_contents=$(dpkg-deb -c "${DOWNLOADS}/blue-linux-amd64.deb" 2>/dev/null | awk '{print $NF}')
check_no_leak "debian deb"

if [[ $FAIL -eq 1 ]]; then
  echo ""
  echo "PIPELINE FAILED: source leaks detected." >&2
  exit 1
fi

echo ""
echo "══════════════════════════════════════════"
echo " Pipeline complete. All packages clean."
echo "══════════════════════════════════════════"
# ── 5. Windows installer .exe ─────────────────────────────────────────────────
echo ""
echo "── Phase 5: Building Windows installer ──"
if command -v makensis >/dev/null 2>&1; then
  bash "${ROOT}/build/make-windows-installer.sh"
else
  echo "  makensis not found - skipping installer (sudo apt install nsis)"
fi

echo ""
echo "══════════════════════════════════════════"
echo " Pipeline complete. All packages clean."
echo "══════════════════════════════════════════"
echo ""
echo "Next steps:"
echo "  1. Rebuild landing binary: ./blue_bin -build examples/bluejs-landing -o /tmp/bluejs_landing_new"
echo "  2. Deploy: bash build/deploy.sh  (or upload manually)"
