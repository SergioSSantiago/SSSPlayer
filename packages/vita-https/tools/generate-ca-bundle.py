#!/usr/bin/env python3
"""Generate the embedded CA bundle from one pinned certifi cacert.pem."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--certifi-version", required=True)
    parser.add_argument("--sha256", required=True)
    args = parser.parse_args()

    data = args.input.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != args.sha256.lower():
        raise SystemExit(
            f"CA bundle hash mismatch: expected {args.sha256}, got {digest}"
        )
    if b"-----BEGIN CERTIFICATE-----" not in data:
        raise SystemExit("input does not look like a PEM certificate bundle")

    lines = [
        "/* Generated file: do not edit by hand.",
        f" * Source: certifi {args.certifi_version} cacert.pem",
        f" * SHA-256: {digest}",
        " * Generator: tools/generate-ca-bundle.py",
        " */",
        "",
        '#include "ca_bundle.h"',
        "",
        f"const unsigned char ca_bundle_pem[{len(data)}] = {{",
    ]
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        lines.append("  " + ",".join(str(value) for value in chunk) + ",")
    lines.extend(
        [
            "};",
            f"const size_t ca_bundle_pem_len = {len(data)};",
            "",
        ]
    )
    args.output.write_text("\n".join(lines), encoding="ascii", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
