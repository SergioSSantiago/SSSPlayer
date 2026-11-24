#!/usr/bin/env bash
# Native Windows + MSYS2 UCRT64 SSSPlayer builder.
# Prerequisites: bootstrap-vitasdk.ps1, vdpm libraries and pinned dependency builders.
set -euo pipefail

if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
  echo "Run this in the MSYS2 UCRT64 terminal (MSYSTEM=UCRT64)." >&2
  exit 1
fi
root="$(cd "$(dirname "$0")/.." && pwd -P)"
# Use the helper to keep workspace-relative SDK/dependency paths in sync.
source "$root/scripts/env-msys2.sh"
: "${VITASDK:?Set VITASDK to the workspace sibling vitasdk directory}"
if [[ ! -f "$VITASDK/share/vita.toolchain.cmake" ]]; then
  echo "Missing VitaSDK toolchain: $VITASDK/share/vita.toolchain.cmake" >&2
  exit 1
fi
for tool in cmake ninja; do
  command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }
done

cd "$root"
# The top-level script selects a separate debug or release cache.
build_dir="${SSSPLAYER_BUILD_DIR:-$root/build}"
for dep in \
  $SSSPLAYER_DEPS_DIR/ffmpeg-vita-hw/lib/libavcodec.a \
  $SSSPLAYER_DEPS_DIR/libssh2-vita/lib/libssh2.a \
  $SSSPLAYER_DEPS_DIR/jansson-vita/lib/libjansson.a \
  $SSSPLAYER_DEPS_DIR/quirc-vita/lib/libquirc.a \
  $SSSPLAYER_DEPS_DIR/curl-mbedtls/lib/libcurl.a \
  $SSSPLAYER_DEPS_DIR/stb/include/stb_image.h; do
  [[ -s "$dep" ]] || { echo "Missing pinned dependency: $dep" >&2; exit 1; }
done

cmake -S . -B "$build_dir" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DSSSPLAYER_DEPS_DIR="$SSSPLAYER_DEPS_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSSSPLAYER_DEBUG_LOG="${SSSPLAYER_DEBUG_LOG:-OFF}" \
  -DSSSPLAYER_ENABLE_LTO=OFF \
  -DSSSPLAYER_FAST_DEV=ON

version="$(grep -E 'set\(SSSPLAYER_VERSION_LABEL' CMakeLists.txt | sed -E 's/.*"([^"]+)".*/\1/')"
if [[ -z "$version" ]]; then
  echo "Cannot determine SSSPLAYER_VERSION_LABEL" >&2
  exit 1
fi
target="SSSPlayer-${version}.vpk-vpk"
cmake --build "$build_dir" --parallel "${SSSPLAYER_JOBS:-4}" --target "$target"
echo "VPK output: $build_dir/SSSPlayer-${version}.vpk"
