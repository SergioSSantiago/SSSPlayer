#!/usr/bin/env bash
set -euo pipefail

: "${VITASDK:?Set VITASDK before running this script}"

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
curl_version=8.21.0
curl_sha256=aa1b66a70eace83dc624508745646c08ae561de512ab403adffb93ac87fc72e6
curl_url="https://github.com/curl/curl/releases/download/curl-8_21_0/curl-${curl_version}.tar.xz"
mbedtls_version=3.6.5
mbedtls_sha256=4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8
mbedtls_url="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${mbedtls_version}/mbedtls-${mbedtls_version}.tar.bz2"
mbedtls_vita_patch="$repo_root/patches/mbedtls-vita.patch"
mbedtls_vita_patch_sha256=9a035c50864c082c95240ffe39327fb30086c3b30c9b3a81e4f65e51e2b9bbe8
prefix="${VITA_HTTPS_CURL_ROOT:-$repo_root/build/deps/curl-mbedtls}"
jobs="${VITA_HTTPS_BUILD_JOBS:-4}"
work="$(mktemp -d "${TMPDIR:-/tmp}/vita-https-curl.XXXXXX")"
trap 'rm -rf "$work"' EXIT

if [[ ! -x "$VITASDK/bin/arm-vita-eabi-gcc" ]]; then
  echo "VitaSDK was not found at $VITASDK" >&2
  exit 1
fi
if [[ ! -f "$VITASDK/arm-vita-eabi/include/mbedtls/build_info.h" ]]; then
  echo "Install the VitaSDK mbedtls package before building vita-https" >&2
  exit 1
fi
printf '%s  %s\n' "$mbedtls_vita_patch_sha256" "$mbedtls_vita_patch" | \
  shasum -a 256 -c -
if ! grep -Eq 'MBEDTLS_VERSION_NUMBER[[:space:]]+0x03[0-9A-Fa-f]{6}' \
    "$VITASDK/arm-vita-eabi/include/mbedtls/build_info.h"; then
  echo "vita-https requires the VitaSDK Mbed TLS 3.x package" >&2
  exit 1
fi

download_and_verify() {
  local url="$1" output="$2" expected="$3"
  curl -L --fail --retry 3 --max-time 300 "$url" -o "$output"
  printf '%s  %s\n' "$expected" "$output" | shasum -a 256 -c -
}

download_and_verify "$curl_url" "$work/curl.tar.xz" "$curl_sha256"
download_and_verify "$mbedtls_url" "$work/mbedtls.tar.bz2" "$mbedtls_sha256"
tar -xf "$work/curl.tar.xz" -C "$work"
tar -xf "$work/mbedtls.tar.bz2" -C "$work"

cmake -S "$work/curl-${curl_version}" -B "$work/curl-build" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_CURL_EXE=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
  -DENABLE_IPV6=OFF -DCURL_DISABLE_SOCKETPAIR=ON \
  -DHAVE_FCNTL_O_NONBLOCK=OFF -DENABLE_THREADED_RESOLVER=OFF \
  -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF \
  -DENABLE_CURL_MANUAL=OFF -DCURL_USE_LIBPSL=OFF \
  -DCURL_USE_MBEDTLS=ON -DHAVE_PIPE2=0 \
  -DCMAKE_DISABLE_FIND_PACKAGE_Threads=ON
cmake --build "$work/curl-build" --parallel "$jobs"
cmake --install "$work/curl-build"

mkdir -p "$prefix/share/licenses" "$prefix/share/sources"
install -m 0644 "$work/curl-${curl_version}/COPYING" \
  "$prefix/share/licenses/curl.txt"
install -m 0644 "$work/mbedtls-${mbedtls_version}/LICENSE" \
  "$prefix/share/licenses/Mbed-TLS.txt"
install -m 0644 "$VITASDK/arm-vita-eabi/include/zlib.h" \
  "$prefix/share/licenses/zlib-header-with-license.txt"
install -m 0644 "$VITASDK/arm-vita-eabi/lib/pkgconfig/libzstd.pc" \
  "$prefix/share/licenses/zstd-package-notice.txt"
install -m 0644 "$work/curl.tar.xz" \
  "$prefix/share/sources/curl-${curl_version}.tar.xz"
install -m 0644 "$work/mbedtls.tar.bz2" \
  "$prefix/share/sources/mbedtls-${mbedtls_version}.tar.bz2"
install -m 0644 "$mbedtls_vita_patch" \
  "$prefix/share/sources/mbedtls-vita.patch"

nm_tool="$VITASDK/bin/arm-vita-eabi-gcc-nm"
nm_output="$work/libcurl.undefined.txt"
"$nm_tool" -u "$prefix/lib/libcurl.a" > "$nm_output"
if grep -Eq 'OPENSSL_|SSL_(CTX|connect|read|write)' "$nm_output"; then
  echo "Refusing libcurl: OpenSSL symbols were detected" >&2
  exit 1
fi
if ! grep -q 'mbedtls_' "$nm_output"; then
  echo "Refusing libcurl: Mbed TLS symbols were not detected" >&2
  exit 1
fi

cat > "$prefix/share/BUILD-PROVENANCE.txt" <<EOF
curl.version=$curl_version
curl.sha256=$curl_sha256
curl.url=$curl_url
curl.tls_backend=Mbed TLS
mbedtls.version=$mbedtls_version
mbedtls.sha256=$mbedtls_sha256
mbedtls.url=$mbedtls_url
mbedtls.vita_patch.sha256=$mbedtls_vita_patch_sha256
vitasdk.packages.commit=3c2a993afd5f30cb820cb900e7bc7f4d2b45d308
vitasdk.mbedtls.headers=$VITASDK/arm-vita-eabi/include/mbedtls
EOF

echo "Hardened curl/Mbed TLS transport is ready at $prefix"
