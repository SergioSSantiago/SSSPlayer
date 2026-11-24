#!/usr/bin/env bash
# SSSPlayer - MSYS2 UCRT64 single-build launcher.
# One VPK per run. Dependency caches remain shared between debug and release.
set -Eeuo pipefail

usage() {
  cat <<'EOF'
Usage: ./build.sh [--clean] [--deps | -dep] [--debug] [--jobs N]

  (no flags)      Build one VPK; prepare missing dependencies.
  --clean         Delete generated application build caches, not dependencies.
  --deps, -dep    Rebuild every pinned dependency (including FFmpeg), and app.
  --debug         Build one diagnostic VPK with bounded session logging.
  --jobs N        Parallel jobs (default: 4).
  --help          Show this message.

Outputs dist/SSSPlayer-<version>.vpk (or _debug.vpk).
The obsolete _safe VPK and SAFE/UNSAFE build directories are removed.
EOF
}

CLEAN=0
REBUILD_DEPS=0
DEBUG=0
JOBS="${SSSPLAYER_JOBS:-4}"
while (($#)); do
  case "$1" in
    --clean) CLEAN=1 ;;
    --debug) DEBUG=1 ;;
    --safe|--unsafe) echo "Only one build variant remains; do not use $1." >&2; exit 2 ;;
    --deps|-dep|--clean-deps) REBUILD_DEPS=1; CLEAN=1 ;;
    --jobs)
      [[ $# -ge 2 ]] || { echo 'Missing --jobs value' >&2; exit 2; }
      JOBS="$2"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown flag: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] && ((JOBS <= 32)) || {
  echo 'Jobs must be 1-32.' >&2; exit 2;
}
[[ "${MSYSTEM:-}" == UCRT64 ]] || {
  echo 'Run from an existing MSYS2 UCRT64 terminal (not PowerShell).' >&2; exit 1;
}
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$ROOT"
source "$ROOT/scripts/env-msys2.sh"
export SSSPLAYER_JOBS="$JOBS" VITAMEDIADECK_FFMPEG_JOBS="$JOBS" VITA_HTTPS_BUILD_JOBS="$JOBS"
export VITA_HTTPS_CURL_ROOT="$SSSPLAYER_DEPS_DIR/curl-mbedtls"

[[ -f "$VITASDK/share/vita.toolchain.cmake" && -x "$VITASDK/bin/arm-vita-eabi-gcc" ]] || {
  echo "VitaSDK is missing or incomplete: $VITASDK" >&2; exit 1;
}
for tool in cmake ninja git make patch unzip sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || { echo "Missing host utility: $tool" >&2; exit 1; }
done

# VitaSDK libpng 2026.08 sometimes fails to install the compatibility alias
# on Windows when symlink creation is disabled. libpng16.a is the real archive.
PNG_DIR="$VITASDK/arm-vita-eabi/lib"
if [[ ! -f "$PNG_DIR/libpng.a" && -s "$PNG_DIR/libpng16.a" ]]; then
  cp -n "$PNG_DIR/libpng16.a" "$PNG_DIR/libpng.a"
  echo 'Repaired missing VitaSDK libpng.a alias using libpng16.a.'
fi
[[ -s "$PNG_DIR/libpng.a" ]] || {
  echo 'VitaSDK libpng.a is missing (and libpng16.a is unavailable).' >&2; exit 1;
}

# Only delete generated directories under the resolved project/deps paths.
# Source tarballs / pinned Git checkouts under ../deps/src are kept as caches.
if ((REBUILD_DEPS)); then
  [[ "$SSSPLAYER_DEPS_DIR" != "$ROOT" && "$SSSPLAYER_DEPS_DIR" != / &&
     "$SSSPLAYER_DEPS_DIR" != "$HOME" ]] || {
    echo 'Unsafe dependency directory; refusing --deps.' >&2; exit 1;
  }
  echo 'Rebuilding all pinned dependencies; retaining downloaded source caches.'
  for name in stb quirc-vita quirc-vita-objects jansson-vita \
              libssh2-vita libssh2-build curl-mbedtls ffmpeg-vita-hw \
              release-licenses release-sources; do
    path="$SSSPLAYER_DEPS_DIR/$name"
    [[ ! -L "$path" ]] || { echo "Refusing to delete symlink: $path" >&2; exit 1; }
    rm -rf -- "$path"
  done
fi

build_dep() {
  local label="$1" script="$2"; shift 2
  local file missing=0
  for file in "$@"; do [[ -s "$file" ]] || missing=1; done
  if ((missing)); then
    printf '\n== Building %s ==\n' "$label"
    bash "$ROOT/$script"
    for file in "$@"; do
      [[ -s "$file" ]] || { echo "Dependency missing after build: $file" >&2; exit 1; }
    done
  else
    printf 'OK: %s (cached)\n' "$label"
  fi
}
D="$SSSPLAYER_DEPS_DIR"
build_dep stb tools/prepare-stb-image.sh "$D/stb/include/stb_image.h" "$D/stb/LICENSE"
build_dep quirc tools/build-quirc-vita.sh "$D/quirc-vita/lib/libquirc.a"
build_dep Jansson tools/build-jansson-vita.sh "$D/jansson-vita/lib/libjansson.a"
build_dep libssh2 tools/build-libssh2-vita.sh "$D/libssh2-vita/lib/libssh2.a"
build_dep curl packages/vita-https/tools/build-curl-mbedtls.sh "$D/curl-mbedtls/lib/libcurl.a"
# The bundled software audio player requires FFmpeg's AAC decoder.
# Invalidate only the FFmpeg cache when an older profile lacks that decoder.
codec_profile="$D/ffmpeg-vita-hw/share/sssplayer-codec-profile.txt"
ffmpeg_archive="$D/ffmpeg-vita-hw/lib/libavcodec.a"
ffmpeg_profile='avi-msmpeg4v3-mp3-aac-audio-v4'
if [[ -f "$codec_profile" && -s "$ffmpeg_archive" ]]; then
  rebuild_ffmpeg=0
  [[ "$(cat "$codec_profile")" == "$ffmpeg_profile" ]] || rebuild_ffmpeg=1
  # The profile is a recipe marker; verify the actual linked archive too.
  if ! "$VITASDK/bin/arm-vita-eabi-gcc-nm" -g --defined-only "$ffmpeg_archive" |
       grep -F 'ff_aac_decoder' >/dev/null; then
    rebuild_ffmpeg=1
  fi
  if ((rebuild_ffmpeg)); then
    [[ ! -L "$D/ffmpeg-vita-hw" ]] || { echo 'Refusing symlinked FFmpeg directory.' >&2; exit 1; }
    echo 'Rebuilding FFmpeg: AAC software decoder is missing or profile is obsolete.'
    rm -rf -- "$D/ffmpeg-vita-hw"
  fi
fi
build_dep 'FFmpeg H.264 + MPEG-4 + AAC/MP3' tools/build-ffmpeg-vita-hw.sh \
  "$D/ffmpeg-vita-hw/lib/libavcodec.a" \
  "$D/ffmpeg-vita-hw/lib/libavformat.a" \
  "$D/ffmpeg-vita-hw/lib/libavutil.a" \
  "$D/ffmpeg-vita-hw/share/sssplayer-codec-profile.txt"
build_dep 'license/source archive' tools/prepare-release-licenses.sh \
  "$D/release-licenses/PROVENANCE.txt"

if ((CLEAN)); then
  [[ ! -L "$ROOT/build" ]] || { echo 'Refusing to clean symlinked build/.' >&2; exit 1; }
  echo 'Cleaning project CMake cache and objects (./build/ only).'
  rm -rf -- "$ROOT/build"
fi

# Keep the single release cache separate from the debug instrumentation.
VERSION="$(sed -n 's/^[[:space:]]*set(SSSPLAYER_VERSION_LABEL[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/CMakeLists.txt" | head -n 1)"
[[ -n "$VERSION" ]] || { echo 'Cannot find version in CMakeLists.txt' >&2; exit 1; }
mkdir -p "$ROOT/dist"
if ((DEBUG)); then
  export SSSPLAYER_DEBUG_LOG=ON
  filename="SSSPlayer-${VERSION}_debug.vpk"
  export SSSPLAYER_BUILD_DIR="$ROOT/build/debug"
  echo 'Debug logging ON: session_log.txt is capped at 256 KiB per session.'
else
  export SSSPLAYER_DEBUG_LOG=OFF
  filename="SSSPlayer-${VERSION}.vpk"
  export SSSPLAYER_BUILD_DIR="$ROOT/build/release"
fi

# One standard VPK is easier to test; the old SAFE variants did not change
# the firmware warning. Remove only generated artifacts, never source files.
# Never follow symlinks.
for old_cache in unsafe safe unsafe-debug safe-debug; do
  old_path="$ROOT/build/$old_cache"
  [[ ! -L "$old_path" ]] || { echo "Refusing symlinked cache: $old_path" >&2; exit 1; }
  if [[ -d "$old_path" ]]; then
    rm -rf -- "$old_path"
    echo "Removed legacy build cache: build/$old_cache"
  fi
done
for old_vpk in "$ROOT"/dist/SSSPlayer-*_safe.vpk \
               "$ROOT"/dist/SSSPlayer-*_safe_debug.vpk; do
  [[ -L "$old_vpk" ]] && { echo "Refusing symlinked VPK: $old_vpk" >&2; exit 1; }
  [[ -f "$old_vpk" ]] && rm -f -- "$old_vpk"
done

printf '\n== Building SSSPlayer VPK ==\n'
bash "$ROOT/scripts/build-windows-msys2.sh"
vpk="$SSSPLAYER_BUILD_DIR/SSSPlayer-${VERSION}.vpk"
[[ -s "$vpk" ]] || { echo "Missing VPK: $vpk" >&2; exit 1; }
unzip -tq "$vpk" >/dev/null
python "$ROOT/scripts/check-vpk-permissions.py" "$vpk"
cp -f -- "$vpk" "$ROOT/dist/$filename"
sha256sum "$ROOT/dist/$filename"
echo "SUCCESS: $ROOT/dist/$filename"
unset SSSPLAYER_BUILD_DIR SSSPLAYER_DEBUG_LOG

# Keep an editable French INI beside the release packages.
# Do not make translators unpack a VPK just to review text.
# The VPK already includes the same file at app0:data/lang/fr.ini.
# For custom translations on Vita, copy it to ux0:data/SSSPlayer/lang/fr.ini.
french_ini_source="$ROOT/data/lang/fr.ini"
french_ini_dest="$ROOT/dist/data/lang/fr.ini"
[[ -s "$french_ini_source" ]] || {
  echo "Missing French translation: $french_ini_source" >&2; exit 1;
}
mkdir -p -- "$(dirname -- "$french_ini_dest")"
if [[ ! -f "$french_ini_dest" ]] || ! cmp -s "$french_ini_source" "$french_ini_dest"; then
  cp -f -- "$french_ini_source" "$french_ini_dest"
fi
echo "French translation: $french_ini_dest"

# The Asian glyph font is a separate optional add-on: shipping it inside every
# VPK wastes download/install space and may increase font initialization work.
# Copy it to dist when present, but do not make it mandatory.
font_source="$ROOT/optional-data/SSSPlayer/fonts"
font_dest="$ROOT/dist/optional-data/SSSPlayer/fonts"
if [[ -s "$font_source/NotoSansJP-Regular.ttf" ]]; then
  mkdir -p "$font_dest"
  for name in NotoSansJP-Regular.ttf OFL.txt; do
    if [[ -f "$font_source/$name" ]]; then
      if [[ ! -f "$font_dest/$name" ]] || ! cmp -s "$font_source/$name" "$font_dest/$name"; then
        cp -f -- "$font_source/$name" "$font_dest/$name"
      fi
    fi
  done
  echo "Optional CJK font: $font_dest/NotoSansJP-Regular.ttf"
  echo 'On Vita, place it at ux0:data/SSSPlayer/fonts/NotoSansJP-Regular.ttf'
else
  echo 'Optional CJK font not found in optional-data/SSSPlayer/fonts/; skipping dist copy.'
fi

# True-black OLED palettes leave background pixels unlit on OLED Vitas.
# Export the four color variants so users may edit colors without recompiling.
for color in red black blue white; do
  theme_source="$ROOT/themes/oled-$color/theme.ini"
  theme_dest="$ROOT/dist/optional-data/SSSPlayer/themes/oled-$color"
  [[ -f "$theme_source" ]] || continue
  mkdir -p "$theme_dest"
  cp -f -- "$theme_source" "$theme_dest/theme.ini"
done
