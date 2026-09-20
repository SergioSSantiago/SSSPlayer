VitaMediaDeck-derived components in SSSPlayer are distributed under GPL-3.0-only.
See `LICENSE`. The following third-party notices apply to packaged dependencies.

Where a dependency offers dual licensing, SSSPlayer selects the terms listed
below for the binary distribution.

## Decoder and network packages

- **vita-hw-decoder**, **vita-sw-decoder**, **vita-https** — see their in-tree
  `LICENSE` / `THIRD_PARTY_NOTICES.md` under `packages/`.

## Notable third-party components

- **FFmpeg** (LGPL-2.1) with wiliwili Vita hardware decoder contributions (GPL-3.0)
- **Mbed TLS**, **curl**, **libssh2**, **libsmb2**, **libxml2**, **Jansson**
- **mpg123**, **FLAC**, **libogg**, **libvorbis**
- **vita2d**, **FreeType**, **libpng**, **libjpeg-turbo**, **libwebp**
- **quirc**, **stb_image**, **pthread-embedded**, **reAvPlayer**

Full license texts are bundled in the VPK under `licenses/` when building a
release with the prepare-release-licenses tooling.

SSSPlayer is an independent homebrew project by SergioSSantiago.
PlayStation is a trademark of Sony Interactive Entertainment.
