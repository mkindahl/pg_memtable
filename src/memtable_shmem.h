#ifndef MEMTABLE_SHMEM_H
#define MEMTABLE_SHMEM_H

#include "memtable.h"

/* The arenas: the shared memory segments that hold the rows of the tables */

/*
 * Arena header
 */

typedef struct MemTableArenaHeader {
  LWLock lock;       /* serialises allocation */
  uint64 tuple_free; /* bump-allocator cursor: next free byte */
  uint64 size;       /* segment capacity, fixed at creation */
} MemTableArenaHeader;

dsm_handle memtable_create_arena(MemTableStateCacheEntry* entry);

void memtable_release_arena(dsm_handle handle);

extern int memtable_arena_size_kb;

void memtable_shmem_init(void);

#endif /* MEMTABLE_SHMEM_H */
