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
 * relation, so it is only made once. If the handle of the entry does not
 * match relfilenode, another backend has truncated the table and replaced
 * the arena, so the old mapping is detached and the new one is made.
 */

/*
 * Find the arena of a relation, attaching it if this backend has not done so
 * yet.
 */
MemTableStateCacheEntry* memtable_get_state(Relation rel) {
  dsm_handle handle = (dsm_handle)rel->rd_locator.relNumber;
  bool found;
  MemTableStateCacheEntry* entry =
      memtable_cache_enter(RelationGetRelid(rel), &found);

  /* The arena of a table that is being created is not in pg_class yet. */
  if (found && (entry->handle == handle ||
                memtable_is_finalize_pending(RelationGetRelid(rel))))
    return entry;

  if (found && entry->seg != NULL) {
    dsm_detach(entry->seg); /* the arena was replaced by another backend */
    entry->seg = NULL;
  }

  memtable_attach_arena(rel, entry);
  return entry;
}
