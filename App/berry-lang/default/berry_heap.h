/********************************************************************
** Berry per-VM heap sandbox -- TLSF arena (plan W8).
**
** Each Berry VM runs inside a fixed chunk of the system heap, managed by a
** private TLSF allocator instance: the chunk is malloc'd on VM create and
** freed on VM destroy. This bounds a VM's RAM (a runaway script throws
** BE_MALLOC_FAIL instead of starving the firmware heap) and returns all of
** its memory on teardown.
**
** All Berry allocation funnels through be_arena_malloc/realloc/free (wired via
** BE_EXPLICIT_* in berry_conf.h; verified: every malloc/free/realloc in
** berry-lang/src lives in be_mem.c). Those operate on the currently-*active*
** arena.
**
** Cooperative single-VM model: exactly one arena is active at a time. Callers
** set the active arena around a VM's lifetime and restore the previous one on
** exit (so non-overlapping nesting is safe). NOT safe for concurrent VMs
** without a per-context active pointer (see plan W5/W8 multi-VM notes).
********************************************************************/
#ifndef BERRY_HEAP_H
#define BERRY_HEAP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle to a TLSF-managed system-heap chunk. */
typedef struct berry_heap_arena berry_heap_arena_t;

/**
 * @brief malloc @p bytes from the system heap and init a TLSF arena in it.
 * @return arena handle, or NULL if the system heap can't supply the chunk or
 *         @p bytes is too small for the TLSF control structure.
 */
berry_heap_arena_t *px_berry_heap_create(size_t bytes);

/** @brief Free the arena's system-heap chunk and its handle. NULL-safe. */
void v_berry_heap_destroy(berry_heap_arena_t *px_arena);

/**
 * @brief Select the arena Berry allocates from.
 * @return the previously-active arena (save it; restore on exit). NULL selects
 *         "no active arena" -- Berry must not allocate in that state.
 */
berry_heap_arena_t *px_berry_heap_set_active(berry_heap_arena_t *px_arena);

/** @brief Bytes currently free in @p px_arena (introspection/tuning); 0 if NULL. */
size_t sz_berry_heap_free_bytes(const berry_heap_arena_t *px_arena);

#ifdef __cplusplus
}
#endif

#endif /* BERRY_HEAP_H */
