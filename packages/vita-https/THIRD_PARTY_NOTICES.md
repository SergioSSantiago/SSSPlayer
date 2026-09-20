# vita-https third-party notices

The wrapper sources are distributed under GPL-3.0-only; see `LICENSE`.

The package links a pinned Vita libcurl build using Mbed TLS plus the VitaSDK
ports of Mbed TLS and zlib/zstd. Their
licenses and source-offer requirements continue to apply to distributed
binaries:

- libcurl: curl license, https://curl.se/docs/copyright.html
- Mbed TLS 3.6.5: Apache-2.0 OR GPL-2.0-or-later; this package selects
  Apache-2.0, https://github.com/Mbed-TLS/mbedtls
- zlib: zlib license, https://zlib.net/zlib_license.html
- Zstandard: BSD-3-Clause/GPL-2.0 dual license, https://github.com/facebook/zstd
- VitaSDK: https://vitasdk.org/

`src/ca_bundle.c` embeds the certifi 2026.07.22 Mozilla root-CA bundle. Its
source hash and reproducible generator are recorded in `CA_BUNDLE_PROVENANCE.md`
and `tools/generate-ca-bundle.py`. Mozilla's CA Certificate Policy and the
Mozilla Public License 2.0 apply to the source data.
