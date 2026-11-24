#!/usr/bin/env bash
# SSSPlayer / VitaMediaDeck native Windows 11 toolchain setup.
# Run ONLY in the MSYS2 UCRT64 shell. No WSL required.
# Source bundle: SSSPlayer-Windows11-MSYS2-dev-bundle.zip (20 Nov 2026).
# Official VitaSDK channel: 2026.08.
#
# Usage: bash setup-sssplayer-msys2.sh [--toolchain-only] [--no-build]
#        [--skip-update] [--project /c/work/custom/gamedev/psvita/SSSPlayer-main]
#        [--archive /c/path/SSSPlayer-Windows11-MSYS2-dev-bundle.zip]
#        [--jobs 4]
#
# Rerunnable: skips completed project dependencies, retains sibling ../deps,
# never overwrites an existing SDK or replaces an existing source tree.
set -Eeuo pipefail

usage() {
  cat <<'EOF'
SSSPlayer native Windows + MSYS2 UCRT64 installer

  --toolchain-only     Install MSYS2 host tools, VitaSDK and vdpm libraries only
  --no-build           Prepare SDK and pinned dependencies, but do not build VPK
  --skip-update        Do not run pacman -Syu (only if MSYS2 is already current)
  --project PATH       Project source directory (default: directory containing script)
  --archive PATH       Provided development ZIP (default: search beside script/cwd)
  --jobs N             Build parallelism (default: 4)
  -h, --help           Show this help

Place this script in SSSPlayer-main/ (the existing modified source tree),
open "MSYS2 UCRT64" in that directory, and run: bash ./setup-sssplayer-msys2.sh
Third-party libraries will be written to ../deps and the SDK to ../vitasdk.

If MSYS2 updates its core runtime and requests a restart, CLOSE ALL
MSYS2 terminals, open UCRT64 again, and rerun the same command.
EOF
}

normalize_path() {
  local p="$1"
  if [[ "$p" =~ ^[a-zA-Z]:[\\/] ]]; then
    command -v cygpath >/dev/null 2>&1 || { echo 'cygpath is required' >&2; exit 1; }
    cygpath -u "$p"
  else
    printf '%s\n' "$p"
  fi
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT="${SSSPLAYER_DIR:-$SCRIPT_DIR}"
ARCHIVE="${SSSPLAYER_ARCHIVE:-}"
JOBS="${SSSPLAYER_JOBS:-4}"
ONLY_TOOLCHAIN=0
NO_BUILD=0
SKIP_UPDATE=0

while (($#)); do
  case "$1" in
    --toolchain-only) ONLY_TOOLCHAIN=1; shift ;;
    --no-build) NO_BUILD=1; shift ;;
    --skip-update) SKIP_UPDATE=1; shift ;;
    --project|--archive|--jobs)
      if (($# < 2)); then echo "Missing argument for $1" >&2; exit 2; fi
      case "$1" in
        --project) PROJECT="$2" ;;
        --archive) ARCHIVE="$2" ;;
        --jobs) JOBS="$2" ;;
      esac
      shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ "${MSYSTEM:-}" != UCRT64 ]]; then
  echo 'ERROR: Open the MSYS2 UCRT64 shell (NOT plain MSYS, MINGW64, WSL or Git Bash).' >&2
  exit 1
fi
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]] || ((JOBS > 32)); then
  echo 'ERROR: --jobs must be an integer from 1 to 32.' >&2
  exit 2
fi
PROJECT="$(normalize_path "$PROJECT")"
if [[ -n "$ARCHIVE" ]]; then ARCHIVE="$(normalize_path "$ARCHIVE")"; fi
WORKSPACE_ROOT="$(cd "$(dirname "$PROJECT")" && pwd -P)"
export SSSPLAYER_DEPS_DIR="${SSSPLAYER_DEPS_DIR:-$WORKSPACE_ROOT/deps}"
export VITASDK="${SSSPLAYER_VITASDK_DIR:-$WORKSPACE_ROOT/vitasdk}"
export VITA_HTTPS_CURL_ROOT="$SSSPLAYER_DEPS_DIR/curl-mbedtls"
[[ "$VITASDK" != *[[:space:]]* && "$SSSPLAYER_DEPS_DIR" != *[[:space:]]* ]] || {
  echo 'ERROR: SDK and dependencies paths cannot contain spaces for the FFmpeg build.' >&2; exit 2;
}


# Log away from the project: it also works before the project ZIP is extracted.
LOG_DIR="${SSSPLAYER_LOG_DIR:-$WORKSPACE_ROOT/setup-logs}"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/setup-$(date '+%Y%m%d-%H%M%S')-$$.log"
exec > >(tee -a "$LOG_FILE") 2>&1
STEP='starting'
finish() {
  local status="$1"
  if ((status)); then
    echo
    echo "SETUP FAILED in phase: $STEP (exit $status)"
    echo 'Fix the first reported error, then rerun the script. Prior successful steps are preserved.'
  fi
  if command -v cygpath >/dev/null 2>&1; then
    echo "Full log: $(cygpath -w "$LOG_FILE")"
  else
    echo "Full log: $LOG_FILE"
  fi
}
trap 'finish $?' EXIT
fail() { echo "ERROR: $*" >&2; exit 1; }
section() { STEP="$1"; printf '\n===== %s =====\n' "$STEP"; }

printf 'SSSPlayer native Windows/MSYS2 setup\n'
printf 'Project: %s\nSDK:     %s (VitaSDK 2026.08)\nDeps:    %s\nJobs:    %s\n' \
  "$PROJECT" "$VITASDK" "$SSSPLAYER_DEPS_DIR" "$JOBS"
[[ "$PROJECT" != *[[:space:]]* ]] || fail 'Use a project path without spaces (FFmpeg configure requires it).'

section 'Updating MSYS2 host'
command -v pacman >/dev/null 2>&1 || fail 'pacman not found. Install MSYS2 from https://www.msys2.org/ first.'
if ((SKIP_UPDATE)); then
  echo 'Skipping MSYS2 system upgrade at your request.'
else
  core_before="$(pacman -Q msys2-runtime pacman bash 2>/dev/null || true)"
  echo 'Performing a FULL MSYS2 system upgrade (required by MSYS2).'
  echo 'If MSYS2 asks to close all terminals, do so and rerun this script afterward.'
  pacman -Syu --noconfirm
  core_after="$(pacman -Q msys2-runtime pacman bash 2>/dev/null || true)"
  if [[ "$core_before" != "$core_after" ]]; then
    echo
    echo 'MSYS2 core packages changed. STOP here to avoid using stale DLLs.'
    echo 'Close ALL MSYS2 windows, reopen MSYS2 UCRT64 and rerun the same script.'
    exit 0
  fi
fi

section 'Installing UCRT64 host tools'
pacman -S --needed --noconfirm \
  base-devel git make patch curl wget tar unzip zip \
  perl coreutils diffutils findutils sed grep gawk file python \
  autoconf automake libtool pkgconf \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-gcc

for tool in cmake ninja git make patch shasum unzip sha256sum cygpath powershell.exe; do
  command -v "$tool" >/dev/null 2>&1 || fail "Expected host tool missing: $tool"
done
printf 'CMake: %s\n' "$(command -v cmake)"
printf 'Ninja: %s\n' "$(command -v ninja)"

section 'Bootstrapping native Windows VitaSDK and Vita libraries'
# The SDK's Windows bootstrap and vdpm work in a real Windows PowerShell process.
# A downloaded official bootstrap is executed only if the SDK is not yet installed.
# This process-scoped ExecutionPolicy flag does NOT change system policy.
export VITASDK_WINDOWS="$(cygpath -w "$VITASDK")"
export VITASDK_CHANNEL=2026.08
ps_file="$LOG_DIR/install-vitasdk-$$.ps1"
cat > "$ps_file" <<'PS1'
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$env:VITASDK = $env:VITASDK_WINDOWS
$env:VITASDK_CHANNEL = '2026.08'
$sdk = $env:VITASDK
$vdpm = Join-Path $sdk 'bin\vdpm.exe'
$gcc  = Join-Path $sdk 'bin\arm-vita-eabi-gcc.exe'
$toolchain = Join-Path $sdk 'share\vita.toolchain.cmake'
if (-not (Test-Path -LiteralPath $vdpm)) {
    if (Test-Path -LiteralPath $sdk) {
        throw "Existing $sdk has no vdpm.exe; refusing to overwrite. Inspect or move it manually."
    }
    $url = 'https://github.com/vitasdk/vdpm/releases/latest/download/bootstrap-vitasdk.ps1'
    $download = Join-Path $env:TEMP 'sssplayer-bootstrap-vitasdk.ps1'
    Write-Host "Downloading official VitaSDK bootstrap: $url"
    Invoke-WebRequest -Uri $url -OutFile $download
    if ((Get-Item -LiteralPath $download).Length -lt 100) {
        throw 'Downloaded VitaSDK bootstrap is unexpectedly short.'
    }
    Write-Host 'Executing official VitaSDK bootstrap (channel 2026.08)...'
    & $download
    if (-not (Test-Path -LiteralPath $vdpm)) {
        throw 'Bootstrap returned without creating vdpm.exe.'
    }
} else {
    Write-Host "Existing VitaSDK found at $sdk; it will not be replaced."
}
if (-not (Test-Path -LiteralPath $gcc)) { throw "Missing Vita compiler: $gcc" }
if (-not (Test-Path -LiteralPath $toolchain)) { throw "Missing Vita toolchain: $toolchain" }
& $vdpm status
if ($LASTEXITCODE -ne 0) { throw "vdpm status failed ($LASTEXITCODE)" }
$packages = @(
  'libvita2d', 'freetype', 'libpng', 'libjpeg-turbo', 'libwebp',
  'zlib', 'bzip2', 'mpg123', 'flac', 'libogg', 'libvorbis',
  'mbedtls', 'libxml2', 'zstd', 'libsmb2'
)
Write-Host "Installing Vita-target packages via vdpm: $($packages -join ', ')"
& $vdpm install @packages
if ($LASTEXITCODE -ne 0) { throw "vdpm install failed ($LASTEXITCODE)" }
Write-Host 'Native VitaSDK and target libraries are installed.'
PS1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$ps_file")"

export PATH="$VITASDK/bin:$PATH"
[[ -f "$VITASDK/share/vita.toolchain.cmake" ]] || fail 'VitaSDK CMake toolchain file not found.'
[[ -f "$VITASDK/arm-vita-eabi/lib/libvita2d.a" ]] || fail 'Vita-target libvita2d archive missing.'
[[ -f "$VITASDK/arm-vita-eabi/include/mbedtls/build_info.h" ]] || fail 'Vita-target Mbed TLS header missing.'
"$VITASDK/bin/arm-vita-eabi-gcc" --version | sed -n '1p'

# For each new UCRT64 session, source ./scripts/env-msys2.sh.
# Do not append hard-coded SDK paths to ~/.bashrc; moving psvita stays safe.
if ((ONLY_TOOLCHAIN)); then
  section 'Toolchain installation complete'
  echo 'VitaSDK + vdpm libraries are ready. No project dependencies or VPK were built (--toolchain-only).'
  exit 0
fi

section 'Preparing supplied SSSPlayer modified source'
if [[ -f "$PROJECT/CMakeLists.txt" && -f "$PROJECT/tools/build-ffmpeg-vita-hw.sh" ]]; then
  echo "Using existing source: $PROJECT (not overwritten)."
else
  if [[ -e "$PROJECT" ]]; then
    fail "Project directory already exists but is incomplete: $PROJECT. Move it aside rather than overwriting it."
  fi
  if [[ -z "$ARCHIVE" ]]; then
    for candidate in \
      "$SCRIPT_DIR/SSSPlayer-Windows11-MSYS2-dev-bundle.zip" \
      "$PWD/SSSPlayer-Windows11-MSYS2-dev-bundle.zip"; do
      if [[ -f "$candidate" ]]; then ARCHIVE="$candidate"; break; fi
    done
  fi
  [[ -f "$ARCHIVE" ]] || fail 'Put SSSPlayer-Windows11-MSYS2-dev-bundle.zip beside this script, or use --archive PATH.'
  unzip -Z -1 "$ARCHIVE" | grep -Fx 'SSSPlayer-main/CMakeLists.txt' >/dev/null || \
    fail 'The ZIP does not contain SSSPlayer-main/CMakeLists.txt.'
  parent="$(dirname -- "$PROJECT")"
  mkdir -p "$parent"
  unpack="$(mktemp -d "$parent/.sssplayer-unpack.XXXXXX")"
  # Only extract this one known project folder, not other arbitrary ZIP contents.
  if ! unzip -q "$ARCHIVE" 'SSSPlayer-main/*' -d "$unpack"; then
    rm -rf -- "$unpack"
    fail 'Project ZIP extraction failed.'
  fi
  [[ -f "$unpack/SSSPlayer-main/CMakeLists.txt" ]] || fail 'CMakeLists.txt missing after extraction.'
  mv -- "$unpack/SSSPlayer-main" "$PROJECT"
  rmdir -- "$unpack"
  echo "Extracted supplied modified source to $PROJECT"
fi
cd "$PROJECT"
[[ -f scripts/env-msys2.sh && -f CMakeLists.txt ]] || \
  fail 'Workspace-relative source patch missing. Apply SSSPlayer-MSYS2-workspace-overlay.zip to your source directory first.'
if ! grep -q 'SSSPLAYER_DEPS_DIR' CMakeLists.txt; then
  fail 'Project CMakeLists.txt is still the old layout. Apply the workspace overlay before running setup.'
fi
mkdir -p "$SSSPLAYER_DEPS_DIR"

# This uploaded ZIP is missing tools/patches/jansson-vita-no-pic.patch,
# although tools/build-jansson-vita.sh requires it. Restore a minimal,
# transparent local equivalent for Jansson 2.15.1 (PIC off on static lib).
# The generated patch has a DIFFERENT checksum from release/DEPENDENCIES.lock;
# we record its real hash in the generated release provenance file.
section 'Checking source-bundle compatibility and Jansson patch'
JANSSON_PATCH="$PROJECT/tools/patches/jansson-vita-no-pic.patch"
if [[ ! -f "$JANSSON_PATCH" ]]; then
  mkdir -p "$(dirname "$JANSSON_PATCH")"
  cat > "$JANSSON_PATCH" <<'PATCH'
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -323,2 +323,2 @@
     set_target_properties(jansson PROPERTIES
-       POSITION_INDEPENDENT_CODE true)
+       POSITION_INDEPENDENT_CODE false)
PATCH
  echo 'Created missing Jansson no-PIC patch (local reconstruction).'
  echo 'NOTE: This patch was missing from the bundle; strict release reproducibility is NOT established.'
else
  echo 'Found existing Jansson no-PIC patch; it is left unchanged.'
fi
JANSSON_PATCH_HASH="$(sha256sum "$JANSSON_PATCH" | awk '{print $1}')"
echo "Jansson patch SHA-256: $JANSSON_PATCH_HASH"

export VITAMEDIADECK_FFMPEG_JOBS="$JOBS"
export VITA_HTTPS_BUILD_JOBS="$JOBS"
export SSSPLAYER_JOBS="$JOBS"
# Keep the HTTPS prefix identical in Bash and root CMakeLists.txt.
export VITA_HTTPS_CURL_ROOT="$SSSPLAYER_DEPS_DIR/curl-mbedtls"

run_dep() {
  local label="$1" script="$2"; shift 2
  local output all_present=1
  for output in "$@"; do [[ -s "$output" ]] || all_present=0; done
  section "$label"
  if ((all_present)); then
    echo 'Already built; skipping.'
  else
    bash "$script"
    for output in "$@"; do
      [[ -s "$output" ]] || fail "$label completed without required artifact: $output"
    done
  fi
}

run_dep 'stb_image' tools/prepare-stb-image.sh \
  "$SSSPLAYER_DEPS_DIR/stb/include/stb_image.h" "$SSSPLAYER_DEPS_DIR/stb/LICENSE"
run_dep 'quirc Vita library' tools/build-quirc-vita.sh \
  "$SSSPLAYER_DEPS_DIR/quirc-vita/lib/libquirc.a" "$SSSPLAYER_DEPS_DIR/quirc-vita/include/quirc.h"
run_dep 'Jansson Vita library' tools/build-jansson-vita.sh \
  "$SSSPLAYER_DEPS_DIR/jansson-vita/lib/libjansson.a" "$SSSPLAYER_DEPS_DIR/jansson-vita/LICENSE"
run_dep 'libssh2 Vita library' tools/build-libssh2-vita.sh \
  "$SSSPLAYER_DEPS_DIR/libssh2-vita/lib/libssh2.a" "$SSSPLAYER_DEPS_DIR/libssh2-vita/share/BUILD-PROVENANCE.txt"
run_dep 'curl / Mbed TLS for Vita' packages/vita-https/tools/build-curl-mbedtls.sh \
  "$SSSPLAYER_DEPS_DIR/curl-mbedtls/lib/libcurl.a" "$SSSPLAYER_DEPS_DIR/curl-mbedtls/share/BUILD-PROVENANCE.txt"
run_dep 'custom FFmpeg h264_vita (non-LTO)' tools/build-ffmpeg-vita-hw.sh \
  "$SSSPLAYER_DEPS_DIR/ffmpeg-vita-hw/lib/libavcodec.a" \
  "$SSSPLAYER_DEPS_DIR/ffmpeg-vita-hw/lib/libavformat.a" \
  "$SSSPLAYER_DEPS_DIR/ffmpeg-vita-hw/lib/libavutil.a" \
  "$SSSPLAYER_DEPS_DIR/ffmpeg-vita-hw/share/licenses/FFmpeg-LGPL-2.1.txt"
run_dep 'release licenses and corresponding-source notices' tools/prepare-release-licenses.sh \
  "$SSSPLAYER_DEPS_DIR/release-licenses/PROVENANCE.txt" \
  "$SSSPLAYER_DEPS_DIR/release-licenses/jansson-MIT.txt"

# Correct the generated provenance line when the absent patch was locally
# reconstructed. Do not silently claim its historical pinned hash.
provenance="$SSSPLAYER_DEPS_DIR/release-licenses/PROVENANCE.txt"
if [[ -f "$provenance" ]]; then
  sed -i "s/^jansson\.vita_patch\.sha256=.*/jansson.vita_patch.sha256=$JANSSON_PATCH_HASH/" "$provenance"
  if ! grep -q '^jansson.vita_patch.origin=' "$provenance"; then
    printf '%s\n' 'jansson.vita_patch.origin=local-file-in-source-tree' >> "$provenance"
  fi
fi

if ((NO_BUILD)); then
  section 'Dependency setup complete'
  echo 'All required Vita-target project dependencies are present. Build skipped (--no-build).'
  exit 0
fi

section 'Configuring SSSPlayer with VitaSDK / Ninja'
mkdir -p build
if [[ -f build/CMakeCache.txt ]]; then
  cached_generator="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' build/CMakeCache.txt | head -n 1)"
  # Reconfigure if a previous build cached the old SDK or old in-project dependencies.
  if [[ "$cached_generator" != Ninja ]] || \
     grep -Eq '(/c/vitasdk|/c/dev/SSSPlayer-main|SSSPlayer-main/build/deps|SSSPlayer-main/build/curl-mbedtls)' build/CMakeCache.txt; then
    backup="build/old-cmake-cache-$(date '+%Y%m%d-%H%M%S')"
    mkdir -p "$backup"
    mv build/CMakeCache.txt "$backup/"
    if [[ -d build/CMakeFiles ]]; then mv build/CMakeFiles "$backup/"; fi
    echo "Preserved incompatible CMake cache under $backup; dependency archives untouched."
  fi
fi
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DSSSPLAYER_DEPS_DIR="$SSSPLAYER_DEPS_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSSSPLAYER_ENABLE_LTO=OFF \
  -DSSSPLAYER_FAST_DEV=ON

section 'Compiling and packaging SSSPlayer VPK'
bash scripts/build-windows-msys2.sh
version="$(sed -n 's/^[[:space:]]*set(SSSPLAYER_VERSION_LABEL[[:space:]]*"\([^"]*\)".*/\1/p' CMakeLists.txt | head -n 1)"
[[ -n "$version" ]] || fail 'Could not read SSSPLAYER_VERSION_LABEL from CMakeLists.txt.'
vpk="$PROJECT/build/SSSPlayer-${version}.vpk"
[[ -s "$vpk" ]] || fail "Build finished, but VPK was not found: $vpk"
unzip -tq "$vpk" >/dev/null
sha256sum "$vpk"
printf '\nSUCCESS: SSSPlayer VPK produced and ZIP integrity checked.\n'
printf 'VPK: %s\n' "$(cygpath -w "$vpk")"
echo 'Remember: a successful build does not verify the app on real Vita hardware.'
echo 'Back up your installed app and ux0:data/SSSPlayer/ before testing.'
