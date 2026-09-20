# CA bundle provenance

`src/ca_bundle.c` is generated from `cacert.pem` distributed by certifi. It is
data derived from Mozilla's root certificate program, not an independently
maintained Vita trust list.

- certifi version: `2026.07.22`
- preferred source: `sources/certifi-2026.07.22-cacert.pem`
- source file SHA-256:
  `9cc2a774b5198dcff14d9be1e66091f538975d867ce029a96bce15a55dfd730f`
- upstream: <https://github.com/certifi/python-certifi>
- license notice: `licenses/certifi-MPL-2.0.txt`

Regenerate only from that exact input:

```sh
python3 tools/generate-ca-bundle.py \
  --input sources/certifi-2026.07.22-cacert.pem \
  --output src/ca_bundle.c \
  --certifi-version 2026.07.22 \
  --sha256 9cc2a774b5198dcff14d9be1e66091f538975d867ce029a96bce15a55dfd730f
```

Changing certifi versions is a reviewed security update: update the version,
expected hash, notice and generated source in the same commit.
