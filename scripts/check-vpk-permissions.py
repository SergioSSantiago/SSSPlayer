#!/usr/bin/env python3
"""Check VPK executable permissions and LiveArea-compatible SFO flags.

This is a package sanity check, not proof of behavior on real hardware.
"""

import argparse
import struct
import sys
import zipfile
from pathlib import Path

RESTRICTED_AUTH_ID = 0x2F00000000000002
SFO_MAGIC = 0x46535000
SFO_FORBIDDEN_LIVEAREA_FLAGS = 0x00080008
SFO_KEEP_BACKGROUND_ON_CLOSE = 0x04000000


def read_sfo_attribute(data: bytes) -> int:
    """@brief Read the 32-bit ATTRIBUTE entry from sce_sys/param.sfo.

    @param data Complete SFO file bytes.
    @return The ATTRIBUTE bitmask.
    @raises ValueError If the SFO is missing, malformed, or truncated.
    """
    if len(data) < 20:
        raise ValueError("param.sfo has no complete header")
    magic, _version, keys_offset, values_offset, count = struct.unpack_from("<5I", data)
    if magic != SFO_MAGIC or count > (len(data) - 20) // 16:
        raise ValueError("param.sfo has an invalid header or entry count")
    for index in range(count):
        key_offset, _alignment, _value_type, value_len, _max_len, value_offset = (
            struct.unpack_from("<HBBIII", data, 20 + 16 * index)
        )
        key_start = keys_offset + key_offset
        if key_start >= len(data):
            raise ValueError("param.sfo key offset is out of range")
        key_end = data.find(b"\0", key_start)
        if key_end < 0:
            raise ValueError("param.sfo contains an unterminated key")
        if data[key_start:key_end] != b"ATTRIBUTE":
            continue
        value_start = values_offset + value_offset
        if value_len != 4 or value_start + 4 > len(data):
            raise ValueError("param.sfo ATTRIBUTE is not a 32-bit integer")
        return struct.unpack_from("<I", data, value_start)[0]
    raise ValueError("param.sfo has no ATTRIBUTE entry")


def inspect(vpk: Path):
    """@brief Inspect a VPK's executable auth IDs and LiveArea flags.

    @param vpk Path to the installed-package candidate.
    @return A tuple of executable auth IDs and the SFO ATTRIBUTE value.
    """
    entries = []
    with zipfile.ZipFile(vpk) as bundle:
        try:
            attribute = read_sfo_attribute(bundle.read("sce_sys/param.sfo"))
        except KeyError as error:
            raise ValueError("sce_sys/param.sfo is absent") from error
        for info in bundle.infolist():
            path = info.filename.lower()
            if not (path == "eboot.bin" or path.endswith((".suprx", ".self", ".skprx"))):
                continue
            with bundle.open(info) as stream:
                header = stream.read(0x88)
            if len(header) < 0x88 or header[:3] != b"SCE":
                raise ValueError(f"{info.filename}: not a valid SCE executable header")
            authid = int.from_bytes(header[0x80:0x88], "little")
            entries.append((info.filename, authid))
    if not any(name.lower() == "eboot.bin" for name, _ in entries):
        raise ValueError("eboot.bin is absent")
    return entries, attribute


# VitaShell checks imports in addition to the FSELF authorization ID.
# These names trigger its extended-permissions warning when imported.
VITASHELL_UNSAFE_IMPORTS = (b"ScePromoterUtil", b"SceShellSvc")


def find_vitashell_unsafe_imports(elf_path: Path) -> list[str]:
    """@brief Detect known privileged import names in the linked ELF.

    @param elf_path Path to the unwrapped Vita ELF used to produce eboot.bin.
    @return Names VitaShell would classify as unsafe, if present.
    @raises ValueError If the input is not an ELF executable.
    """
    data = elf_path.read_bytes()
    if not data.startswith(b"\x7fELF"):
        raise ValueError(f"{elf_path}: not an ELF executable")
    return [name.decode("ascii") for name in VITASHELL_UNSAFE_IMPORTS
            if name + b"\0" in data]


def main():
    """@brief Check one VPK and optionally report privileged ELF imports.

    @return Zero on a compatible package, one on any detected problem.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument("vpk", type=Path)
    parser.add_argument("--elf", type=Path,
                        help="linked ELF to check for VitaShell-unsafe import names")
    args = parser.parse_args()
    try:
        entries, attribute = inspect(args.vpk)
    except (OSError, zipfile.BadZipFile, ValueError) as error:
        print(f"VPK verification failed: {error}", file=sys.stderr)
        return 1

    status = 0
    for path, authid in entries:
        restricted = authid == RESTRICTED_AUTH_ID
        print(f"{path}: 0x{authid:016X} ({'restricted' if restricted else 'extended'})")

    if args.elf:
        try:
            unsafe_imports = find_vitashell_unsafe_imports(args.elf)
        except (OSError, ValueError) as error:
            print(f"ELF import verification failed: {error}", file=sys.stderr)
            return 1
        if unsafe_imports:
            print("VitaShell-unsafe ELF imports: " + ", ".join(unsafe_imports),
                  file=sys.stderr)
        else:
            print("ELF import preflight: no known VitaShell-unsafe libraries")

    blocked = attribute & (SFO_FORBIDDEN_LIVEAREA_FLAGS | SFO_KEEP_BACKGROUND_ON_CLOSE)
    print(f"sce_sys/param.sfo: ATTRIBUTE=0x{attribute:08X} (blocked=0x{blocked:08X})")
    if blocked:
        print("VPK disables normal LiveArea suspend/close behavior.", file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    raise SystemExit(main())
