#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
: "${VITASDK:?Set VITASDK before auditing}"
curl_root="${VITA_HTTPS_CURL_ROOT:-$repo_root/build/deps/curl-mbedtls}"
for file in "$repo_root/LICENSE" "$repo_root/THIRD_PARTY_NOTICES.md" \
  "$repo_root/DEPENDENCIES.lock" "$repo_root/CA_BUNDLE_PROVENANCE.md" \
  "$repo_root/licenses/certifi-MPL-2.0.txt" \
  "$repo_root/sources/certifi-2026.07.22-cacert.pem" \
  "$curl_root/lib/libcurl.a" "$curl_root/share/BUILD-PROVENANCE.txt" \
  "$curl_root/share/sources/curl-8.21.0.tar.xz" \
  "$curl_root/share/sources/mbedtls-3.6.5.tar.bz2"; do
  [[ -f "$file" ]] || { echo "Missing $file" >&2; exit 1; }
done
printf '%s  %s\n' \
  9a035c50864c082c95240ffe39327fb30086c3b30c9b3a81e4f65e51e2b9bbe8 \
  "$repo_root/patches/mbedtls-vita.patch" | shasum -a 256 -c - >/dev/null
printf '%s  %s\n' \
  9cc2a774b5198dcff14d9be1e66091f538975d867ce029a96bce15a55dfd730f \
  "$repo_root/sources/certifi-2026.07.22-cacert.pem" | shasum -a 256 -c - >/dev/null
generated="$(mktemp "${TMPDIR:-/tmp}/vita-https-ca.XXXXXX.c")"
nm_output="$(mktemp "${TMPDIR:-/tmp}/vita-https-nm.XXXXXX")"
trap 'rm -f "$generated" "$nm_output"' EXIT
python3 "$repo_root/tools/generate-ca-bundle.py" \
  --input "$repo_root/sources/certifi-2026.07.22-cacert.pem" \
  --output "$generated" --certifi-version 2026.07.22 \
  --sha256 9cc2a774b5198dcff14d9be1e66091f538975d867ce029a96bce15a55dfd730f
cmp -s "$generated" "$repo_root/src/ca_bundle.c" || {
  echo "Generated CA source does not match the pinned PEM" >&2; exit 1;
}
grep -q 'CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2' \
  "$repo_root/src/vita_https.c" || {
  echo "TLS 1.2 minimum policy is missing" >&2; exit 1;
}
"$VITASDK/bin/arm-vita-eabi-gcc-nm" -u "$curl_root/lib/libcurl.a" > "$nm_output"
grep -q 'mbedtls_' "$nm_output" || { echo "Mbed TLS symbols are missing" >&2; exit 1; }
if grep -Eq 'OPENSSL_|SSL_(CTX|connect|read|write)' "$nm_output"; then
  echo "OpenSSL symbols are forbidden in a release" >&2; exit 1
fi
echo "vita-https release audit passed"
