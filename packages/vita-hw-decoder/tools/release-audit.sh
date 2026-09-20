#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
: "${VITASDK:?Set VITASDK before auditing}"
ffmpeg="${VITA_HW_DECODER_FFMPEG_ROOT:-$repo_root/build/deps/ffmpeg-vita-hw}"
for file in LICENSE THIRD_PARTY_NOTICES.md DEPENDENCIES.lock \
  "$ffmpeg/share/licenses/FFmpeg-LGPL-2.1.txt" \
  "$ffmpeg/share/licenses/wiliwili-GPL-3.0.txt" \
  "$ffmpeg/share/sources/ffmpeg-n6.0-h264-vita-source.tar.gz" \
  "$ffmpeg/share/sources/wiliwili-88e5876b.tar.gz" \
  "$ffmpeg/share/sources/wiliwili-ffmpeg.patch"; do
  [[ -f "$repo_root/$file" || -f "$file" ]] || { echo "Missing $file" >&2; exit 1; }
done
nm_output="$(mktemp "${TMPDIR:-/tmp}/vita-hw-nm.XXXXXX")"
trap 'rm -f "$nm_output"' EXIT
"$VITASDK/bin/arm-vita-eabi-gcc-nm" -g --defined-only \
  "$ffmpeg/lib/libavcodec.a" > "$nm_output"
grep -q 'ff_h264_vita' "$nm_output" || {
  echo "h264_vita symbol is missing" >&2; exit 1;
}
echo "vita-hw-decoder release audit passed"
