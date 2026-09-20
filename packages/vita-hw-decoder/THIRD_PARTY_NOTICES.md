# Vita HW Player third-party notices

The module is distributed under GPL-3.0-only; see `LICENSE`.

It is designed for the pinned FFmpeg `n6.0` source and the `h264_vita` patch
from wiliwili. The selected FFmpeg configuration is LGPL-2.1-or-later and
wiliwili is GPL-3.0. Embedding applications must provide the corresponding
source, license texts, and VitaSDK port notices when distributing binaries.
`tools/build-ffmpeg.sh` installs the exact patched FFmpeg and wiliwili source
archives beside the generated libraries for that purpose.

- https://github.com/FFmpeg/FFmpeg
- https://github.com/xfangfang/wiliwili
- https://vitasdk.org/

The runtime-loading path expects ReAvPlayer, distributed under the MIT License:
https://github.com/SonicMastr/ReAvPlayer
