#include "memtable_cache.h"

static HTAB* local_state_cache = NULL;

/*
 * Get a memtable cache entry or create it if not found.
 */
MemTableStateCacheEntry* memtable_cache_enter(Oid relid, bool* found) {
  MemTableStateCacheEntry* entry;

  if (local_state_cache == NULL) {
    HASHCTL ctl = {
        .keysize = sizeof(Oid),
        .entrysize = sizeof(MemTableStateCacheEntry),
    };

    local_state_cache =
        hash_create("memtable local state", 16, &ctl, HASH_ELEM | HASH_BLOBS);
  }

  entry = hash_search(local_state_cache, &relid, HASH_ENTER, found);
  if (!*found) {
    entry->seg = NULL;
    entry->arena_base = NULL;
    entry->hdr = NULL;
    entry->handle = DSM_HANDLE_INVALID;
  }

  return entry;
}

/* Find the cache entry of a relation, if it has an arena attached. */
MemTableStateCacheEntry* memtable_cache_find(Oid relid) {
  MemTableStateCacheEntry* entry = NULL;

  if (local_state_cache != NULL) {
    entry = hash_search(local_state_cache, &relid, HASH_FIND, NULL);
    if (entry == NULL || entry->seg == NULL)
      entry = NULL;
  }

  return entry;
}

/* Remove the cache entry of a relation. */
void memtable_cache_remove(Oid relid) {
  if (local_state_cache != NULL)
    hash_search(local_state_cache, &relid, HASH_REMOVE, NULL);
}

/*
 * Finding the arena from any backend
 *
 * relfilenode holds the handle of the arena, so rd_locator.relNumber is all
 * that is needed to find it. The mapping is kept in the cache entry of the
 * relation, so it is only made once. The relcache callback below removes the
 * entry when the relation is invalidated, so an entry that is found always
 * refers to the current arena.
 */

/*
 * Find the arena of a relation, attaching it if this backend has not done so
 * yet.
 */
MemTableStateCacheEntry* memtable_get_state(Relation rel) {
  Oid relid = RelationGetRelid(rel);
  bool found;
  MemTableStateCacheEntry* entry = memtable_cache_enter(relid, &found);

  if (!found)
    memtable_attach_arena(rel, entry);
  return entry;
}

/*
 * Dropping cache entries when the relation changes
 *
 * A mapping stays in the cache after it has been made, which means that a
 * backend that has read a table keeps the arena alive after another backend
 * has dropped or truncated it. A relcache invalidation is sent for the
 * relation in both cases, and it is always processed before this backend
 * locks the relation again, so the callback gets the relation id in time to
 * remove the entry before the next call to memtable_get_state.
 *
 * The callback can be called at any time, even in the middle of a scan, and
 * a scan holds a copy of the entry that points into the mapping. The entry
 * is therefore removed at once, but the mapping is only recorded and is
 * detached when the transaction ends.
 *
 * The exception is an arena that this transaction created. Its entry is how
 * memtable_finalize_new_relation finds it, and the invalidation of the new
 * pg_class row arrives before that call, so the entry has to stay.
 */

static List* stale_handles = NIL;

static void memtable_forget_entry(MemTableStateCacheEntry* entry) {
  if (entry->seg == NULL || memtable_is_new_arena(entry))
    return;

  if (!list_member_oid(stale_handles, (Oid)entry->handle))
    stale_handles = lappend_oid(stale_handles, (Oid)entry->handle);
  memtable_cache_remove(entry->relid);
}

/* Called when a relation is invalidated: forget its cache entry. */
void memtable_relcache_callback(Datum arg, Oid relid) {
  MemoryContext oldcxt = MemoryContextSwitchTo(TopMemoryContext);

  if (relid == InvalidOid) {
    if (local_state_cache != NULL) {
      HASH_SEQ_STATUS status;
      MemTableStateCacheEntry* entry;

      hash_seq_init(&status, local_state_cache);
      while ((entry = hash_seq_search(&status)) != NULL)
        memtable_forget_entry(entry);
    }
  } else {
    MemTableStateCacheEntry* entry = memtable_cache_find(relid);

    if (entry != NULL)
      memtable_forget_entry(entry);
  }

  MemoryContextSwitchTo(oldcxt);
}

/*
 * Detach the mappings that the callback gave up, unless the entry of some
 * relation has started using the same segment again in the meantime. A
 * mapping that was already detached, for example by memtable_release_arena
 * when the table was dropped, is not found and is skipped.
 */
static bool memtable_handle_in_use(dsm_handle handle) {
  HASH_SEQ_STATUS status;
  MemTableStateCacheEntry* entry;

  if (local_state_cache == NULL)
    return false;

  hash_seq_init(&status, local_state_cache);
  while ((entry = hash_seq_search(&status)) != NULL) {
    if (entry->handle == handle) {
      hash_seq_term(&status);
      return true;
    }
  }
  return false;
}

/* Detach the mappings that the relcache callback has given up. */
void memtable_detach_stale_mappings(void) {
  ListCell* lc;

  foreach (lc, stale_handles) {
    dsm_handle handle = (dsm_handle)lfirst_oid(lc);
    dsm_segment* seg = dsm_find_mapping(handle);

    if (seg != NULL && !memtable_handle_in_use(handle))
      dsm_detach(seg);
  }

  list_free(stale_handles);
  stale_handles = NIL;
}
