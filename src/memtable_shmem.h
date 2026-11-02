#ifndef MEMTABLE_SHMEM_H
#define MEMTABLE_SHMEM_H

#include "memtable.h"

/* The arenas: the shared memory segments that hold the rows of the tables */

/*
 * Arena header
 */

typedef struct MemTableArenaHeader {
  LWLock lock;                 /* serialises allocation */
  pg_atomic_uint64 tuple_free; /* bump-allocator cursor: next free byte */
  uint64 size;                 /* segment capacity, fixed at creation */
} MemTableArenaHeader;

typedef struct MemTableSlotHeader {
  uint32 tuple_size; /* byte length of the HeapTupleHeaderData that follows */
  uint32 padding;    /* keeps the tuple that follows MAXALIGNed */
} MemTableSlotHeader;

dsm_handle memtable_create_arena(MemTableStateCacheEntry* entry);

void memtable_attach_arena(Relation rel, MemTableStateCacheEntry* entry);

void memtable_release_arena(dsm_handle handle);

arena_off_t memtable_arena_reserve(MemTableStateCacheEntry* local, size_t size);

void memtable_arena_publish(MemTableStateCacheEntry* local, size_t size);

extern int memtable_arena_size_kb;

void memtable_shmem_init(void);

#endif /* MEMTABLE_SHMEM_H */
