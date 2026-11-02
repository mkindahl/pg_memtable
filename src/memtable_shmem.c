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
  pg_atomic_init_u64(&hdr->tuple_free, MAXALIGN(sizeof(MemTableArenaHeader)));
  hdr->size = arena_size;

  entry->seg = seg;
  entry->arena_base = arena_base;
  entry->hdr = hdr;
  entry->handle = handle;

  return handle;
}

/*
 * Attach this backend to the arena that relfilenode names and fill in the
 * cache entry.
 *
 * dsm_attach() raises an error if the segment is already mapped in this
 * backend, so look for an existing mapping first. A new mapping is pinned,
 * like the one made when the arena is created. Otherwise it would belong to
 * the current resource owner, and Postgres warns about resources that are
 * still open when the transaction commits. Pinning a mapping that is already
 * pinned does nothing.
 *
 * The segment is gone after a server restart, since the handle in
 * relfilenode outlives the segments. The entry is removed again in that case,
 * so that the next call does not find an entry without a mapping.
 */
void memtable_attach_arena(Relation rel, MemTableStateCacheEntry* entry) {
  dsm_handle handle = (dsm_handle)rel->rd_locator.relNumber;
  dsm_segment* seg = dsm_find_mapping(handle);

  if (seg == NULL) {
    seg = dsm_attach(handle);
    if (seg == NULL) {
      memtable_cache_remove(entry->relid);
      ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                      errmsg("could not attach to arena %u", handle)));
    }
  }

  dsm_pin_mapping(seg); /* survive end-of-transaction in this backend */

  entry->seg = seg;
  entry->arena_base = dsm_segment_address(seg);
  entry->hdr = entry->arena_base;
  entry->handle = handle;
}

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
 * Allocating space in the arena
 *
 * A row is written in three steps: reserve the space, write the row, and
 * publish it. The arena lock is held from the reservation to the publication,
 * so there is one writer at a time. A scan reads the cursor, which only moves
 * when a row is published, and never looks past it, so it cannot see a row
 * that is not completely written.
 */

/*
 * Reserve space for a tuple at the end of the arena and return its offset. The
 * arena lock is held until the tuple has been published.
 */
arena_off_t memtable_arena_reserve(MemTableStateCacheEntry* local,
                                   size_t size) {
  MemTableArenaHeader* hdr = local->hdr;
  uint64 off;

  LWLockAcquire(&hdr->lock, LW_EXCLUSIVE);
  off = pg_atomic_read_u64(&hdr->tuple_free);
  if (off + MAXALIGN(size) > hdr->size) {
    LWLockRelease(&hdr->lock);
    ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                    errmsg("memtable arena is full"),
                    errhint("Increase memtable.arena_size before the table is "
                            "created.")));
  }
  return off;
}

/*
 * Make the tuple that was written to the reserved space visible to scans and
 * release the arena lock.
 */
void memtable_arena_publish(MemTableStateCacheEntry* local, size_t size) {
  MemTableArenaHeader* hdr = local->hdr;

  pg_write_barrier();
  pg_atomic_write_u64(&hdr->tuple_free,
                      pg_atomic_read_u64(&hdr->tuple_free) + MAXALIGN(size));
  LWLockRelease(&hdr->lock);
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
