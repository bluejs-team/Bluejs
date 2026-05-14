#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${RAYLIB_VERSION:-5.5}"
SRC="$ROOT/.raylib/src"
PREFIX="$ROOT/.raylib/install"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
ARCHIVE="$ROOT/.raylib/raylib-$VERSION.tar.gz"

mkdir -p "$ROOT/.raylib"
rm -rf "$SRC" "$PREFIX"
mkdir -p "$SRC" "$PREFIX"

if [ ! -f "$ARCHIVE" ]; then
  curl -L "https://github.com/raysan5/raylib/archive/refs/tags/$VERSION.tar.gz" -o "$ARCHIVE"
fi

tar -xzf "$ARCHIVE" --strip-components=1 -C "$SRC"

cmake -S "$SRC" -B "$SRC/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON

cmake --build "$SRC/build" -j"$JOBS"
cmake --install "$SRC/build"

echo "raylib $VERSION installed to $PREFIX"
