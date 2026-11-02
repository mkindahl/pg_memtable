#include "memtable_shmem.h"

#include "memtable_cache.h"

static int MemTableArenaTranche = -1;

int memtable_arena_size_kb =
    64 * 1024; /* 64 MiB default, configurable up to 4 GiB */

static shmem_startup_hook_type prev_shmem_startup = NULL;

static void memtable_shmem_startup_hook(void) {
  if (prev_shmem_startup)
    prev_shmem_startup();

  MemTableArenaTranche = LWLockNewTrancheId();
  LWLockRegisterTranche(MemTableArenaTranche, MEMTABLE_EXTNAME "_arena");
}

/*
 * Arena creation
 */

/* Create and pin an arena for a relation and record it in the cache entry. */
dsm_handle memtable_create_arena(MemTableStateCacheEntry* entry) {
  size_t arena_size = (size_t)memtable_arena_size_kb * 1024;
  dsm_segment* seg = dsm_create(arena_size, 0);
  dsm_handle handle = dsm_segment_handle(seg);
  void* arena_base = dsm_segment_address(seg);
  MemTableArenaHeader* hdr = arena_base;

  dsm_pin_segment(seg); /* survive backend detach */
  dsm_pin_mapping(seg); /* survive end-of-transaction in this backend */

  memset(hdr, 0, sizeof(*hdr));
  LWLockInitialize(&hdr->lock, MemTableArenaTranche);
  hdr->tuple_free = MAXALIGN(sizeof(MemTableArenaHeader));
  hdr->size = arena_size;

  entry->seg = seg;
  entry->arena_base = arena_base;
  entry->hdr = hdr;
  entry->handle = handle;

  return handle;
}

/*
 * DROP TABLE
 */

/*
 * Does the segment with the given handle still exist? A restart of the server
 * destroys every segment, but the handles stay in pg_class.relfilenode.
 */
static bool memtable_segment_exists(dsm_handle handle) {
  dsm_segment* seg = dsm_find_mapping(handle);

  if (seg != NULL)
    return true;

  seg = dsm_attach(handle); /* NULL if there is no such segment */
  if (seg == NULL)
    return false;

  dsm_detach(seg);
  return true;
}

/* Unpin the arena with the given handle and detach this backend from it. */
void memtable_release_arena(dsm_handle handle) {
  dsm_segment* seg;

  if (!memtable_segment_exists(handle))
    return;

  seg = dsm_find_mapping(handle);
  dsm_unpin_segment(handle); /* must precede the final detach */
  if (seg != NULL)
    dsm_detach(seg);
}

/*
 * Define the GUC that sets the size of an arena, and install the hook that
 * registers the LWLock tranche of the arenas.
 */
void memtable_shmem_init(void) {
  DefineCustomIntVariable(
      "memtable.arena_size",
      "DSM arena size per memtable relation (default 64 MB).", NULL,
      &memtable_arena_size_kb, 65536, 64, 4 * 1024 * 1024, PGC_USERSET,
      GUC_UNIT_KB, NULL, NULL, NULL);

  prev_shmem_startup = shmem_startup_hook;
  shmem_startup_hook = memtable_shmem_startup_hook;
}
