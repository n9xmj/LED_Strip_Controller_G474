# Vendored: TLSF (Two-Level Segregated Fit) allocator

O(1) malloc/free/realloc with bounded fragmentation — the within-block
sub-allocator backing the per-Berry-VM heap sandbox (plan W8). Used so each
Berry VM gets a runtime-sized chunk of the system heap, allocates entirely
inside it, and returns the whole chunk on VM destroy.

## Provenance

- **Upstream:** https://github.com/mattconte/tlsf
- **Version:** 3.1 (Matthew Conte)
- **Commit:** `deff9ab509341f264addbd3c8ada533678591905` (2020-03-29)
- **License:** BSD-3-Clause (see [LICENSE](LICENSE); full text also in the
  `tlsf.c` / `tlsf.h` headers). Permissive — attribution retained.
- **Local changes:** none (pristine upstream `tlsf.c` + `tlsf.h`).

## Vendoring method (no submodule)

Cloned to a throwaway scratch dir, then `tlsf.c`, `tlsf.h`, and `README.md`
were copied here — the clone (incl. `.git`) was discarded, so this tree never
contained git metadata. Same no-submodule / own-tree-under-`App/` convention as
`App/berry-lang/`. To update: re-clone upstream, diff, drop in the new
`tlsf.c`/`tlsf.h`, and bump the commit above.

## Build notes

- Pure C, freestanding (`<stddef.h>`/`<stdlib.h>` only — no OS dependency).
- Keep `src/` (here: `tlsf.c`/`tlsf.h`) pristine; any glue (the active-arena
  shim wiring `BE_EXPLICIT_MALLOC/REALLOC/FREE` to a TLSF instance) lives in the
  Berry port layer (`App/berry-lang/default/`), not in this tree.
- Add to the CubeIDE build (source + include path) alongside the W8 glue.
