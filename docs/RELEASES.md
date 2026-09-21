# GitHub releases

## Policy (mandatory)

- **Keep every published release.** Do not delete older tags or VPK assets.
- Users on an older build need a newer GitHub “latest” to exercise in-app Check for updates.
- Prefer `gh release create vX.Y.Z …` without deleting previous releases.

## In-app update vs VitaShell

Install path matches [VitaShell](https://github.com/TheOfficialFloW/VitaShell) `package_installer.c`:

1. Download VPK (HTTPS)
2. Extract to `ux0:data/pkg`
3. Generate `sce_sys/package/head.bin`
4. `loadScePaf` → `scePromoterUtilityPromotePkgWithRif(path, 1)`
5. Clean `ux0:data/pkg`

**Limitation:** VitaShell installs *another* TITLEID while it runs. SSSPlayer promotes **itself** (`SSSP00001`). That can fail or hang on some firmwares. On failure the app leaves `ux0:SSSPlayer-update.vpk` for manual VitaShell install (same promote path, other process).
