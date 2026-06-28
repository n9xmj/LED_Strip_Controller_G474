# Vendored: littlefs

- **Upstream:** https://github.com/littlefs-project/littlefs
- **Version:** v2.11.3 (commit `6cb4e86`)
- **License:** BSD-3-Clause (see `LICENSE.md`)
- **Imported:** 2026-06-27 via shallow clone; git metadata (`.git`, `.github`,
  `.gitignore`, `.gitattributes`) stripped — this is plain vendored source, **not** a submodule.

## Build status

`App/littlefs/` is **excluded from the firmware build** (CubeIDE `.cproject`, Debug + Release)
until the block-device shim lands (plan row G7). Only these four files are ever compiled into
firmware: `lfs.c`, `lfs.h`, `lfs_util.c`, `lfs_util.h`. The upstream test/bench scaffolding
(`runners/`, `tests/`, `benches/`, `scripts/`, `bd/`) is kept for reference but never built.

## Local-modification policy

Core files are kept **pristine** for cheap upstream version bumps; project-specific code (the
SPI-flash block-device shim, any `lfs_util` config) lives in `App/spiflash/`, not in these files.
We retain the freedom to patch core if ever required — diff against the v2.11.3 tag above.

See `Docs/planning/spiflash-driver-implementation-plan.md` (D2) for the integration plan.
