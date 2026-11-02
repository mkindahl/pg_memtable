#include "memtable_cache.h"

/*
 * Table scan callbacks
 */

typedef struct MemTableScanData {
  TableScanDescData rs_base;
  MemTableStateCacheEntry state;
  arena_off_t scan_next;
  arena_off_t scan_stop;
  HeapTupleData htup;
} MemTableScanData;

/*
 * Slot related callbacks
 */

/* The kind of tuple table slot that the executor uses for the table. */
static const TupleTableSlotOps* memtable_slot_callbacks(Relation rel) {
  return &TTSOpsHeapTuple;
}

/* Start a scan of the table. */
static TableScanDesc memtable_scan_begin(Relation rel, Snapshot snapshot,
                                         int nkeys, struct ScanKeyData* key,
                                         ParallelTableScanDesc pscan,
                                         uint32 flags) {
  MemTableScanData* scan = palloc0(sizeof(MemTableScanData));

  scan->rs_base.rs_rd = rel;
  scan->rs_base.rs_snapshot = snapshot;
  scan->rs_base.rs_nkeys = nkeys;
  scan->rs_base.rs_flags = flags;

  scan->state = *memtable_get_state(rel);
  scan->scan_next = MAXALIGN(sizeof(MemTableArenaHeader));
  scan->scan_stop = pg_atomic_read_membarrier_u64(&scan->state.hdr->tuple_free);
  scan->htup.t_tableOid = RelationGetRelid(rel);

  return &scan->rs_base;
}

/* End a scan and release what it holds. */
static void memtable_scan_end(TableScanDesc sscan) {
  pfree(sscan);
}

/* Restart a scan that is already open. */
static void memtable_scan_rescan(TableScanDesc sscan, struct ScanKeyData* key,
                                 bool set_params, bool allow_strat,
                                 bool allow_sync, bool allow_pagemode) {
  MemTableScanData* scan = (MemTableScanData*)sscan;

  scan->scan_next = MAXALIGN(sizeof(MemTableArenaHeader));
  scan->scan_stop = pg_atomic_read_membarrier_u64(&scan->state.hdr->tuple_free);
}

/* Store the next visible row of the scan in a slot. */
static bool memtable_scan_getnextslot(TableScanDesc sscan,
                                      ScanDirection direction,
                                      TupleTableSlot* slot) {
  MemTableScanData* scan = (MemTableScanData*)sscan;
  char* base = scan->state.arena_base;

  if (ScanDirectionIsBackward(direction))
    ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                    errmsg("backward scans are not supported")));

  while (scan->scan_next < scan->scan_stop) {
    char* hdr_ptr = base + scan->scan_next;
    MemTableSlotHeader* shdr = (MemTableSlotHeader*)hdr_ptr;
    HeapTupleHeader htup_data =
        (HeapTupleHeader)(hdr_ptr + sizeof(MemTableSlotHeader));
    arena_off_t this_off = scan->scan_next;

    scan->scan_next += MAXALIGN(sizeof(MemTableSlotHeader) + shdr->tuple_size);

    if (!memtable_tuple_visible(htup_data))
      continue;

    scan->htup.t_len = shdr->tuple_size;
    scan->htup.t_data = htup_data;
    memtable_store_offset(this_off, &scan->htup.t_self);
    ExecStoreHeapTuple(&scan->htup, slot, false);
    return true;
  }

  return false;
}

/* Limit a scan to a range of TIDs. */
static void memtable_scan_set_tidrange(TableScanDesc scan, ItemPointer mintid,
                                       ItemPointer maxtid) {
  NOT_IMPLEMENTED;
}

/* Store the next row of a TID range scan in a slot. */
static bool memtable_scan_getnextslot_tidrange(TableScanDesc scan,
                                               ScanDirection direction,
                                               TupleTableSlot* slot) {
  NOT_IMPLEMENTED;
  return false;
}

/*
 * Parallel table scan related functions
 */

/* Size of the shared state of a parallel scan. */
static Size memtable_parallelscan_estimate(Relation rel) {
  NOT_IMPLEMENTED;
  return 0;
}

/* Initialize the shared state of a parallel scan. */
static Size memtable_parallelscan_initialize(Relation rel,
                                             ParallelTableScanDesc pscan) {
  NOT_IMPLEMENTED;
  return 0;
}

/* Reset the shared state of a parallel scan so that it can start over. */
static void memtable_parallelscan_reinitialize(Relation rel,
                                               ParallelTableScanDesc pscan) {
  NOT_IMPLEMENTED;
}

/*
 * Index scan callbacks -- no index yet
 */

/* Start fetching rows by TID for an index scan. */
static struct IndexFetchTableData* memtable_index_fetch_begin(Relation rel) {
  NOT_IMPLEMENTED;
  return NULL;
}

/* Reset an index fetch. */
static void memtable_index_fetch_reset(struct IndexFetchTableData* data) {
  NOT_IMPLEMENTED;
}

/* End an index fetch. */
static void memtable_index_fetch_end(struct IndexFetchTableData* data) {
  NOT_IMPLEMENTED;
}

/* Fetch the row with the given TID for an index scan. */
static bool memtable_index_fetch_tuple(struct IndexFetchTableData* scan,
                                       ItemPointer tid, Snapshot snapshot,
                                       TupleTableSlot* slot, bool* call_again,
                                       bool* all_dead) {
  NOT_IMPLEMENTED;
  return false;
}

/*
 * Callbacks for non-modifying operations on individual tuples
 */

/* Fetch the row with the given TID if it is visible to the snapshot. */
static bool memtable_tuple_fetch_row_version(Relation rel, ItemPointer tid,
                                             Snapshot snapshot,
                                             TupleTableSlot* slot) {
  NOT_IMPLEMENTED;
  return false;
}

/* Can the TID refer to a row of the table? */
static bool memtable_tuple_tid_valid(TableScanDesc scan, ItemPointer tid) {
  NOT_IMPLEMENTED;
  return false;
}

/* Follow the update chain to the latest version of a row. */
static void memtable_tuple_get_latest_tid(TableScanDesc scan, ItemPointer tid) {
  NOT_IMPLEMENTED;
}

/* Is the row in the slot visible to the snapshot? */
static bool memtable_tuple_satisfies_snapshot(Relation rel,
                                              TupleTableSlot* slot,
                                              Snapshot snapshot) {
  NOT_IMPLEMENTED;
  return false;
}

/* Find the index entries that point to dead rows, for index cleanup. */
static TransactionId memtable_index_delete_tuples(Relation rel,
                                                  TM_IndexDeleteOp* delstate) {
  NOT_IMPLEMENTED;
  return InvalidTransactionId;
}

/*
 * Manipulations of physical tuples
 */

/* Insert the row in the slot into the table. */
static void memtable_tuple_insert(Relation rel, TupleTableSlot* slot,
                                  CommandId cid, int options,
                                  struct BulkInsertStateData* bistate) {
  MemTableStateCacheEntry* state = memtable_get_state(rel);
  HeapTuple tup = ExecFetchSlotHeapTuple(slot, true, NULL);
  HeapTuple flat = NULL;
  size_t total;
  arena_off_t off;
  char* dst;
  HeapTupleHeader htup;

  /* Values stored in the TOAST table of another table cannot be pointed to. */
  if (HeapTupleHasExternal(tup)) {
    flat = toast_flatten_tuple(tup, RelationGetDescr(rel));
    tup = flat;
  }

  total = sizeof(MemTableSlotHeader) + tup->t_len;
  off = memtable_arena_reserve(state, total);
  dst = (char*)state->arena_base + off;

  ((MemTableSlotHeader*)dst)->tuple_size = tup->t_len;
  htup = (HeapTupleHeader)(dst + sizeof(MemTableSlotHeader));
  memcpy(htup, tup->t_data, tup->t_len);

  if (flat != NULL)
    heap_freetuple(flat);

  HeapTupleHeaderSetXmin(htup, GetCurrentTransactionId());
  HeapTupleHeaderSetXmax(htup, InvalidTransactionId);
  htup->t_infomask |= HEAP_XMAX_INVALID;

  memtable_arena_publish(state, total);

  memtable_store_offset(off, &slot->tts_tid);
}

/* Insert a row speculatively, for INSERT ... ON CONFLICT. */
static void memtable_tuple_insert_speculative(
    Relation rel, TupleTableSlot* slot, CommandId cid, int options,
    struct BulkInsertStateData* bistate, uint32 specToken) {
  NOT_IMPLEMENTED;
}

/* Finish or undo a speculative insertion. */
static void memtable_tuple_complete_speculative(Relation rel,
                                                TupleTableSlot* slot,
                                                uint32 specToken,
                                                bool succeeded) {
  NOT_IMPLEMENTED;
}

/* Insert several rows at once. */
static void memtable_multi_insert(Relation rel, TupleTableSlot** slots,
                                  int nslots, CommandId cid, int options,
                                  struct BulkInsertStateData* bistate) {
  int i;

  for (i = 0; i < nslots; i++)
    memtable_tuple_insert(rel, slots[i], cid, options, bistate);
}

/* Delete the row with the given TID. */
static TM_Result memtable_tuple_delete(Relation rel, ItemPointer tid,
                                       CommandId cid, Snapshot snapshot,
                                       Snapshot crosscheck, bool wait,
                                       TM_FailureData* tmfd,
                                       bool changingPart) {
  NOT_IMPLEMENTED;
  return TM_Ok;
}

/* Replace the row with the given TID by the row in the slot. */
static TM_Result memtable_tuple_update(Relation rel, ItemPointer otid,
                                       TupleTableSlot* slot, CommandId cid,
                                       Snapshot snapshot, Snapshot crosscheck,
                                       bool wait, TM_FailureData* tmfd,
                                       LockTupleMode* lockmode,
                                       TU_UpdateIndexes* update_indexes) {
  NOT_IMPLEMENTED;
  return TM_Ok;
}

/* Lock the row with the given TID. */
static TM_Result memtable_tuple_lock(Relation rel, ItemPointer tid,
                                     Snapshot snapshot, TupleTableSlot* slot,
                                     CommandId cid, LockTupleMode mode,
                                     LockWaitPolicy wait_policy, uint8 flags,
                                     TM_FailureData* tmfd) {
  NOT_IMPLEMENTED;
  return TM_Ok;
}

/*
 * DDL related functionality
 */

/* Create the storage of a new table, or a new empty storage for TRUNCATE. */
static void memtable_relation_set_new_filelocator(
    Relation rel, const RelFileLocator* newrlocator, char persistence,
    TransactionId* freezeXid, MultiXactId* minmulti) {
  Oid relid = RelationGetRelid(rel);
  dsm_handle old_handle;
  bool found;
  MemTableStateCacheEntry* entry;
  dsm_handle handle;

  if (persistence == RELPERSISTENCE_TEMP)
    ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                    errmsg("temporary tables are not supported by memtable")));

  /*
   * A command that rewrites a table copies the rows into a new table, named
   * pg_temp_<oid> by make_new_heap, and then swaps the files of the two
   * tables. The handle of an arena is a file number that is swapped with the
   * other table, so this cannot work.
   */
  if (strncmp(RelationGetRelationName(rel), "pg_temp_", 8) == 0)
    ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
             errmsg("memtable relations cannot be rewritten"),
             errdetail("ALTER TABLE commands that rewrite the table, REFRESH "
                       "MATERIALIZED VIEW, CLUSTER and VACUUM FULL are not "
                       "supported.")));

  old_handle = memtable_catalog_lookup(relid, NULL);
  entry = memtable_cache_enter(relid, &found);
  handle = memtable_create_arena(entry);

  /* The handle goes into pg_class.relfilenode when the statement is done. */
  memtable_schedule_finalize(relid);

  /* If this transaction aborts, don't leak the arena just created. */
  memtable_schedule_cleanup(relid, handle, XACT_EVENT_ABORT);

  /* If this transaction commits, and there were an old arena, clean it up. */
  if (old_handle != DSM_HANDLE_INVALID)
    memtable_schedule_cleanup(relid, old_handle, XACT_EVENT_COMMIT);

  *freezeXid = InvalidTransactionId;
  *minmulti = InvalidMultiXactId;
}

/* Empty a table in place, when it was created in the same transaction. */
static void memtable_relation_nontransactional_truncate(Relation rel) {
  MemTableStateCacheEntry* state = memtable_get_state(rel);

  LWLockAcquire(&state->hdr->lock, LW_EXCLUSIVE);
  pg_atomic_write_u64(&state->hdr->tuple_free,
                      MAXALIGN(sizeof(MemTableArenaHeader)));
  LWLockRelease(&state->hdr->lock);
}

/* Copy the storage of a table to a new file locator. */
static void memtable_relation_copy_data(Relation rel,
                                        const RelFileLocator* newrlocator) {
  NOT_IMPLEMENTED;
}

/*
 * Copy the rows of a table in index or sort order, for CLUSTER and VACUUM FULL.
 */
static void memtable_relation_copy_for_cluster(
    Relation OldTable, Relation NewTable, Relation OldIndex, bool use_sort,
    TransactionId OldestXmin, TransactionId* xid_cutoff,
    MultiXactId* multi_cutoff, double* num_tuples, double* tups_vacuumed,
    double* tups_recently_dead) {
  NOT_IMPLEMENTED;
}

/* Vacuum the table. */
static void memtable_relation_vacuum(Relation rel, struct VacuumParams* params,
                                     BufferAccessStrategy bstrategy) {
  NOT_IMPLEMENTED;
}

/* Choose the next block to sample for ANALYZE. */
static bool memtable_scan_analyze_next_block(TableScanDesc scan,
                                             ReadStream* stream) {
  NOT_IMPLEMENTED;
  return false;
}

/* Return the next row of the block that ANALYZE samples. */
static bool memtable_scan_analyze_next_tuple(TableScanDesc scan,
                                             TransactionId OldestXmin,
                                             double* liverows, double* deadrows,
                                             TupleTableSlot* slot) {
  NOT_IMPLEMENTED;
  return false;
}

/* Scan the table to build an index, calling back for every row. */
static double memtable_index_build_range_scan(
    Relation table_rel, Relation index_rel, struct IndexInfo* index_info,
    bool allow_sync, bool anyvisible, bool progress, BlockNumber start_blockno,
    BlockNumber numblocks, IndexBuildCallback callback, void* callback_state,
    TableScanDesc scan) {
  NOT_IMPLEMENTED;
  return 0;
}

/* Scan the table to validate an index that was built concurrently. */
static void memtable_index_validate_scan(Relation table_rel, Relation index_rel,
                                         struct IndexInfo* index_info,
                                         Snapshot snapshot,
                                         struct ValidateIndexState* state) {
  NOT_IMPLEMENTED;
}

/*
 * Miscellaneous functions
 */

/* The size of the table in bytes. */
static uint64 memtable_relation_size(Relation rel, ForkNumber forkNumber) {
  NOT_IMPLEMENTED;
  return 0;
}

/* Does the table need a TOAST table? */
static bool memtable_relation_needs_toast_table(Relation rel) {
  /* Queried during CREATE TABLE itself, so it can't just error out. */
  return false;
}

/* The table access method of the TOAST table. */
static Oid memtable_relation_toast_am(Relation rel) {
  NOT_IMPLEMENTED;
  return InvalidOid;
}

/* Fetch a slice of a value that is stored in the TOAST table. */
static void memtable_relation_fetch_toast_slice(Relation toastrel, Oid valueid,
                                                int32 attrsize,
                                                int32 sliceoffset,
                                                int32 slicelength,
                                                struct varlena* result) {
  NOT_IMPLEMENTED;
}

/*
 * Planner related functions
 */

/* Estimate the number of pages and rows, for the planner. */
static void memtable_relation_estimate_size(Relation rel, int32* attr_widths,
                                            BlockNumber* pages, double* tuples,
                                            double* allvisfrac) {
  /* No real cost model yet -- just enough for the planner to pick a
   * sequential scan without erroring out. */
  *pages = 1;
  *tuples = 0;
  *allvisfrac = 1.0;
}

/*
 * Executor related functions
 */

/* Return the next row of a bitmap heap scan. */
static bool memtable_scan_bitmap_next_tuple(TableScanDesc scan,
                                            TupleTableSlot* slot, bool* recheck,
                                            uint64* lossy_pages,
                                            uint64* exact_pages) {
  NOT_IMPLEMENTED;
  return false;
}

/* Choose the next block of a TABLESAMPLE scan. */
static bool memtable_scan_sample_next_block(TableScanDesc scan,
                                            struct SampleScanState* scanstate) {
  NOT_IMPLEMENTED;
  return false;
}

/* Return the next row of a TABLESAMPLE scan. */
static bool memtable_scan_sample_next_tuple(TableScanDesc scan,
                                            struct SampleScanState* scanstate,
                                            TupleTableSlot* slot) {
  NOT_IMPLEMENTED;
  return false;
}

/*
 * The routine itself
 */

static const TableAmRoutine memtable_methods = {
    .type = T_TableAmRoutine,

    .slot_callbacks = memtable_slot_callbacks,

    .scan_begin = memtable_scan_begin,
    .scan_end = memtable_scan_end,
    .scan_rescan = memtable_scan_rescan,
    .scan_getnextslot = memtable_scan_getnextslot,
    .scan_set_tidrange = memtable_scan_set_tidrange,
    .scan_getnextslot_tidrange = memtable_scan_getnextslot_tidrange,

    .parallelscan_estimate = memtable_parallelscan_estimate,
    .parallelscan_initialize = memtable_parallelscan_initialize,
    .parallelscan_reinitialize = memtable_parallelscan_reinitialize,

    .index_fetch_begin = memtable_index_fetch_begin,
    .index_fetch_reset = memtable_index_fetch_reset,
    .index_fetch_end = memtable_index_fetch_end,
    .index_fetch_tuple = memtable_index_fetch_tuple,

    .tuple_fetch_row_version = memtable_tuple_fetch_row_version,
    .tuple_tid_valid = memtable_tuple_tid_valid,
    .tuple_get_latest_tid = memtable_tuple_get_latest_tid,
    .tuple_satisfies_snapshot = memtable_tuple_satisfies_snapshot,
    .index_delete_tuples = memtable_index_delete_tuples,

    .tuple_insert = memtable_tuple_insert,
    .tuple_insert_speculative = memtable_tuple_insert_speculative,
    .tuple_complete_speculative = memtable_tuple_complete_speculative,
    .multi_insert = memtable_multi_insert,
    .tuple_delete = memtable_tuple_delete,
    .tuple_update = memtable_tuple_update,
    .tuple_lock = memtable_tuple_lock,
    .finish_bulk_insert = NULL, /* the one optional callback */

    .relation_set_new_filelocator = memtable_relation_set_new_filelocator,
    .relation_nontransactional_truncate =
        memtable_relation_nontransactional_truncate,
    .relation_copy_data = memtable_relation_copy_data,
    .relation_copy_for_cluster = memtable_relation_copy_for_cluster,
    .relation_vacuum = memtable_relation_vacuum,
    .scan_analyze_next_block = memtable_scan_analyze_next_block,
    .scan_analyze_next_tuple = memtable_scan_analyze_next_tuple,
    .index_build_range_scan = memtable_index_build_range_scan,
    .index_validate_scan = memtable_index_validate_scan,

    .relation_size = memtable_relation_size,
    .relation_needs_toast_table = memtable_relation_needs_toast_table,
    .relation_toast_am = memtable_relation_toast_am,
    .relation_fetch_toast_slice = memtable_relation_fetch_toast_slice,

    .relation_estimate_size = memtable_relation_estimate_size,

    .scan_bitmap_next_tuple = memtable_scan_bitmap_next_tuple,
    .scan_sample_next_block = memtable_scan_sample_next_block,
    .scan_sample_next_tuple = memtable_scan_sample_next_tuple,
};

PG_FUNCTION_INFO_V1(memtable_handler);

Datum memtable_handler(PG_FUNCTION_ARGS) {
  PG_RETURN_POINTER(&memtable_methods);
}
