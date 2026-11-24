#!/usr/bin/env bash
# Source this from MSYS2 UCRT64: source ./scripts/env-msys2.sh
# All paths are derived from this file, so the whole psvita folder can move.
if [[ "${MSYSTEM:-}" != UCRT64 ]]; then
  echo 'WARNING: Please use the MSYS2 UCRT64 shell.' >&2
fi
_sss_src="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
export SSSPLAYER_DIR="$_sss_src"
export SSSPLAYER_DEPS_DIR="${SSSPLAYER_DEPS_DIR:-$(cd "$_sss_src/.." && pwd -P)/deps}"
export VITASDK="${SSSPLAYER_VITASDK_DIR:-$(cd "$_sss_src/.." && pwd -P)/vitasdk}"
case ":$PATH:" in
  *":$VITASDK/bin:"*) ;;
  *) export PATH="$VITASDK/bin:$PATH" ;;
esac
export VITAMEDIADECK_FFMPEG_JOBS="${VITAMEDIADECK_FFMPEG_JOBS:-4}"
export VITA_HTTPS_BUILD_JOBS="${VITA_HTTPS_BUILD_JOBS:-4}"
export SSSPLAYER_JOBS="${SSSPLAYER_JOBS:-4}"
export VITA_HTTPS_CURL_ROOT="${VITA_HTTPS_CURL_ROOT:-$SSSPLAYER_DEPS_DIR/curl-mbedtls}"
printf 'Project: %s\nSDK: %s\nDeps: %s\n' "$_sss_src" "$VITASDK" "$SSSPLAYER_DEPS_DIR"
unset _sss_src
