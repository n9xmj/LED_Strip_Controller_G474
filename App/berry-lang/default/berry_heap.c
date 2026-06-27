/********************************************************************
** Berry per-VM heap sandbox -- TLSF arena shim (plan W8). See berry_heap.h.
**
** Bridges Berry's BE_EXPLICIT_MALLOC/REALLOC/FREE (berry_conf.h) onto a
** per-VM TLSF instance. The arena chunk comes from the system heap (libc
** malloc) and is handed wholesale to TLSF; Berry then sub-allocates inside it.
********************************************************************/
#include "berry_heap.h"
#include "berry_conf.h"   /* be_arena_malloc/free/realloc decls (def/decl cross-check) */
#include "tlsf.h"

#include <stdlib.h>       /* system malloc/free for the arena chunk + handle */

/* Project error path (Core/Src/main.c). Used as a hard guard if Berry ever
 * allocates with no active arena -- that can only be a session-management bug,
 * and Error_Handler() does not return. */
extern void Error_Handler(void);

struct berry_heap_arena {
    tlsf_t  tlsf;    /* TLSF control -- lives inside `chunk` */
    void   *chunk;   /* system-heap block backing the arena */
    size_t  bytes;   /* size of `chunk` */
};

/* The arena Berry currently allocates from. One active arena at a time
 * (cooperative model); set/restored around each VM's lifetime. */
static berry_heap_arena_t *s_px_active = NULL;

/* ---- arena lifecycle ---------------------------------------------------- */

berry_heap_arena_t *px_berry_heap_create(size_t bytes)
{
    if (bytes <= tlsf_size()) {
        return NULL;   /* too small to hold the TLSF control structure */
    }

    berry_heap_arena_t *px = (berry_heap_arena_t *) malloc(sizeof *px);
    if (px == NULL) {
        return NULL;
    }

    px->chunk = malloc(bytes);
    if (px->chunk == NULL) {
        free(px);
        return NULL;
    }

    px->tlsf = tlsf_create_with_pool(px->chunk, bytes);
    if (px->tlsf == NULL) {
        free(px->chunk);
        free(px);
        return NULL;
    }

    px->bytes = bytes;
    return px;
}

void v_berry_heap_destroy(berry_heap_arena_t *px)
{
    if (px == NULL) {
        return;
    }
    /* The TLSF control lives inside px->chunk, so freeing the chunk reclaims
     * everything the VM allocated -- no per-object teardown needed. */
    free(px->chunk);
    free(px);
}

berry_heap_arena_t *px_berry_heap_set_active(berry_heap_arena_t *px)
{
    berry_heap_arena_t *px_prev = s_px_active;
    s_px_active = px;
    return px_prev;
}

/* ---- introspection ------------------------------------------------------ */

static void v_walk_free(void *pv_ptr, size_t sz, int i_used, void *pv_user)
{
    (void) pv_ptr;
    if (!i_used) {
        *(size_t *) pv_user += sz;
    }
}

size_t sz_berry_heap_free_bytes(const berry_heap_arena_t *px)
{
    if (px == NULL) {
        return 0u;
    }
    size_t sz_free = 0u;
    tlsf_walk_pool(tlsf_get_pool(px->tlsf), v_walk_free, &sz_free);
    return sz_free;
}

/* ---- BE_EXPLICIT_* allocator hooks (wired in berry_conf.h) --------------- */

static tlsf_t active_tlsf(void)
{
    if (s_px_active == NULL) {
        Error_Handler();   /* Berry alloc with no active arena -- does not return */
    }
    return s_px_active->tlsf;
}

void *be_arena_malloc(size_t size)
{
    return tlsf_malloc(active_tlsf(), size);
}

void be_arena_free(void *ptr)
{
    tlsf_free(active_tlsf(), ptr);
}

void *be_arena_realloc(void *ptr, size_t size)
{
    return tlsf_realloc(active_tlsf(), ptr, size);
}
