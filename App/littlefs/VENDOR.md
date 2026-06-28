# Vendored: littlefs

- **Upstream:** https://github.com/littlefs-project/littlefs
- **Version:** v2.11.3 (commit `6cb4e86`)
- **License:** BSD-3-Clause (see `LICENSE.md`)
- **Imported:** 2026-06-27 via shallow clone; git metadata (`.git`, `.github`,
  `.gitignore`, `.gitattributes`) stripped — this is plain vendored source, **not** a submodule.

## Build status

`App/littlefs/` holds **only the files destined for the firmware build** — `lfs.c`, `lfs.h`,
`lfs_util.c`, `lfs_util.h` (+ this `VENDOR.md` and `LICENSE.md`). It is currently **excluded from
the build** (CubeIDE `.cproject`, Debug + Release) until the block-device shim lands (plan row G7).
The upstream test/bench scaffolding and standalone docs (`bd/`, `runners/`, `tests/`, `benches/`,
`scripts/`, `Makefile`, `README.md`, `DESIGN.md`, `SPEC.md`) were moved to
`Docs/littlefs-extras/` — kept for reference, never built.

## Local-modification policy

Core files are kept **pristine** for cheap upstream version bumps; project-specific code (the
SPI-flash block-device shim, any `lfs_util` config) lives in `App/spiflash/`, not in these files.
We retain the freedom to patch core if ever required — diff against the v2.11.3 tag above.

See `Docs/planning/spiflash-driver-implementation-plan.md` (D2) for the integration plan.
