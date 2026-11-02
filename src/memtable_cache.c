#include "memtable_cache.h"

static HTAB* local_state_cache = NULL;

/*
 * Get a memtable cache entry or create it if not found.
 */
MemTableStateCacheEntry* memtable_cache_enter(Oid relid) {
  bool found;
  MemTableStateCacheEntry* entry;

  if (local_state_cache == NULL) {
    HASHCTL ctl = {
        .keysize = sizeof(Oid),
        .entrysize = sizeof(MemTableStateCacheEntry),
    };

    local_state_cache =
        hash_create("memtable local state", 16, &ctl, HASH_ELEM | HASH_BLOBS);
  }

  entry = hash_search(local_state_cache, &relid, HASH_ENTER, &found);
  if (!found) {
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
