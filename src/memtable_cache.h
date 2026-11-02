#ifndef MEMTABLE_CACHE_H
#define MEMTABLE_CACHE_H

#include "memtable_shmem.h"

/* The per-backend cache of the arenas that this backend has attached */

/*
 * Per-backend local state: one entry per attached relation
 */

typedef struct MemTableStateCacheEntry {
  Oid relid; /* hash key */
  dsm_segment* seg;
  void* arena_base;
  MemTableArenaHeader* hdr;
  dsm_handle handle; /* handle of the arena `seg` is attached to */
} MemTableStateCacheEntry;

MemTableStateCacheEntry* memtable_cache_enter(Oid relid);

MemTableStateCacheEntry* memtable_cache_find(Oid relid);

void memtable_cache_remove(Oid relid);

#endif /* MEMTABLE_CACHE_H */
