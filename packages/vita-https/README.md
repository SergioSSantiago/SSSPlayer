# vita-https

Small, secure-by-default HTTPS transport package for PlayStation Vita. It owns
the sceNet/sceNetCtl lifecycle, a pinned libcurl/Mbed TLS stack, Mozilla CA
bundle, authenticated requests and cached seekable Range streams.
Connectivity and Wi-Fi signal queries are guarded by the same lifecycle, so
UI code never calls an unloaded sceNetCtl stub.

It is the extracted transport used by VitaMediaDeck. The protocol is not a custom
TLS implementation: TLS and certificate validation are provided by Mbed TLS;
this package owns the Vita lifecycle, policy and stream abstraction around it.

## Integrate

Build the isolated TLS dependency first. The system VitaSDK libcurl/OpenSSL
archive is deliberately rejected because the installed OpenSSL 1.0.2 port is
end-of-life and has licensing compatibility problems with GPLv3 binaries.

```sh
export VITASDK=/path/to/vitasdk
./tools/build-curl-mbedtls.sh
```

```cmake
add_subdirectory(external/vita-https)
target_link_libraries(my_app PRIVATE VitaHttps::VitaHttps)
```

```c
#include <vita_https.h>

vita_https_init();
VitaHttpsClient *client = vita_https_client_create(NULL);
VitaHttpsRequest request = { .method = "GET", .url = "https://example.com" };
VitaHttpsResponse response;
int result = vita_https_perform(client, &request, &response);
vita_https_client_destroy(client);
vita_https_shutdown();
```

`examples/get.c` includes response buffering. `vita_https_open_range_stream`
returns a seekable 512 KiB-cached cursor after proving support with a real HTTP
`206 Partial Content` response; its five cursor fields map directly onto the
decoder packages' stream handle.

## Security contract

- only `https://` input and redirect URLs are accepted;
- TLS 1.2 is the minimum negotiated protocol version;
- peer and hostname verification are enabled by default;
- a bundled Mozilla CA store is supplied explicitly;
- credentials are never placed in URLs;
- redirects, timeouts, low-speed aborts and cooperative cancellation are
  bounded by the package;
- private or self-signed peers require an exact `sha256//` SPKI pin on every
  request; there is no mode that disables CA verification without a pin.

`vita_https_probe_public_key` performs an unauthenticated, bodyless handshake
solely to obtain the leaf public-key pin for an explicit trust-on-first-use UI.
Do not use the probe result for application data until the user has compared
and confirmed it. A client configured with that confirmed pin may set
`allow_untrusted_ca_with_pin`; the exact public key then replaces public-CA and
hostname trust for that private endpoint, and any later key change fails closed.

The client must outlive every Range stream created from it. Call init/shutdown
symmetrically; the global lifecycle is reference-counted.

## Install and consume

```sh
cmake -S . -B build/package
cmake --build build/package
cmake --install build/package --prefix "$PWD/build/stage"
```

Installed consumers may use `find_package(VitaHttps CONFIG REQUIRED)` and link
`VitaHttps::VitaHttps`. The installed package carries the pinned libcurl
archive, its license texts and corresponding upstream source; Mbed TLS,
zlib/zstd and Vita system stubs are supplied by VitaSDK.

Licensed GPL-3.0-only. See `THIRD_PARTY_NOTICES.md`.
