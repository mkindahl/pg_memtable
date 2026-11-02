#ifndef MEMTABLE_H
#define MEMTABLE_H

#include <postgres.h>
#include <fmgr.h>

#include <access/heapam.h>
#include <access/heaptoast.h>
#include <access/htup_details.h>
#include <access/multixact.h>
#include <access/tableam.h>
#include <access/transam.h>
#include <access/xact.h>
#include <catalog/indexing.h>
#include <catalog/namespace.h>
#include <catalog/objectaccess.h>
#include <catalog/pg_class.h>
#include <commands/defrem.h>
#include <miscadmin.h>
#include <nodes/parsenodes.h>
#include <nodes/pg_list.h>
#include <storage/dsm.h>
#include <storage/ipc.h>
#include <storage/lwlock.h>
#include <tcop/utility.h>
#include <utils/builtins.h>
#include <utils/guc.h>
#include <utils/hsearch.h>
#include <utils/inval.h>
#include <utils/memutils.h>
#include <utils/rel.h>
#include <utils/syscache.h>

#define MEMTABLE_EXTNAME "memtable"

#define NOT_IMPLEMENTED                            \
  ereport(ERROR,                                   \
          (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), \
           errmsg("memtable callback %s not yet implemented", __func__)))

/* The cache entry of a relation, defined in memtable_cache.h */
typedef struct MemTableStateCacheEntry MemTableStateCacheEntry;

/* Types shared by the source files */

typedef uint64 arena_off_t;

/*
 * The TID of a tuple is its arena offset, split into the block number and
 * offset number that the rest of Postgres expects a TID to consist of.
 */

static inline void memtable_store_offset(arena_off_t off, ItemPointer tid) {
  ItemPointerSet(tid, off / BLCKSZ, off % BLCKSZ + 1);
}

/* Support functions in memtable.c that the other files use */

dsm_handle memtable_catalog_lookup(Oid relid, bool* is_memtable);

void memtable_schedule_cleanup(Oid relid, dsm_handle handle, XactEvent event);

bool memtable_is_new_arena(MemTableStateCacheEntry* entry);

bool memtable_tuple_visible(HeapTupleHeader htup);

void memtable_schedule_finalize(Oid relid);

#endif /* MEMTABLE_H */
