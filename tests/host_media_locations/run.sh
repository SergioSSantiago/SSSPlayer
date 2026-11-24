#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -I"$root/tests/host_media_locations/stubs" \
  -I"$root/include" -I"$root/src" \
  "$root/src/media/media_locations.c" \
  "$root/tests/host_media_locations/io_shim.c" \
  "$root/tests/host_media_locations/test_locations.c" -o "$work/test_locations"
(cd "$work" && ./test_locations)
