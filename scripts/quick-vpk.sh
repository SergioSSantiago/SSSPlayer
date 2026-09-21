#!/usr/bin/env bash
# Fast incremental VPK (or ELF) build. Do NOT wipe build/ between runs.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${VITASDK:-/Users/sss/vitasdk}"
export PATH="$VITASDK/bin:${PATH:-/usr/bin:/bin}"
build_dir="${SSSPLAYER_BUILD_DIR:-$repo_root/build}"
jobs="${SSSPLAYER_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 8)}"
# SSSPLAYER_SKIP_VPK=1 → build ELF/self only (fastest compile/link check)
skip_vpk="${SSSPLAYER_SKIP_VPK:-0}"

mkdir -p "$build_dir"
if [[ ! -f "$build_dir/CMakeCache.txt" || "${SSSPLAYER_RECONFIGURE:-0}" == "1" ]]; then
  cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSSSPLAYER_ENABLE_LTO=OFF \
    -DSSSPLAYER_FAST_DEV=ON
fi

version="$(grep -E 'set\(SSSPLAYER_VERSION_LABEL' "$repo_root/CMakeLists.txt" | sed -E 's/.*"([^"]+)".*/\1/')"
if [[ "$skip_vpk" == "1" ]]; then
  target="SSSPlayer"
else
  target="SSSPlayer-${version}.vpk-vpk"
fi

echo "==> incremental build: $target (-j$jobs)"
start=$(date +%s)
cmake --build "$build_dir" -j"$jobs" --target "$target"
elapsed=$(( $(date +%s) - start ))
echo "==> done in ${elapsed}s"
if [[ "$skip_vpk" != "1" ]]; then
  ls -lh "$build_dir/SSSPlayer-${version}.vpk"
fi
