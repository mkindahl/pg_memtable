#include "memtable.h"

#include "memtable_cache.h"
#include "memtable_shmem.h"

PG_MODULE_MAGIC;

static ProcessUtility_hook_type prev_ProcessUtility = NULL;

/*
 * Read pg_class.relfilenode for relid, which holds the DSM handle of the
 * relation's arena, and report whether the relation uses memtable. Returns
 * DSM_HANDLE_INVALID if there is no pg_class row (yet) for relid.
 */
dsm_handle memtable_catalog_lookup(Oid relid, bool* is_memtable) {
  HeapTuple tup = SearchSysCache1(RELOID, ObjectIdGetDatum(relid));

  if (HeapTupleIsValid(tup)) {
    Form_pg_class classForm = (Form_pg_class)GETSTRUCT(tup);
    dsm_handle handle = (dsm_handle)classForm->relfilenode;
    Oid memtable_oid = get_table_am_oid(MEMTABLE_EXTNAME, true);
    bool result = OidIsValid(memtable_oid) && classForm->relam == memtable_oid;

    if (is_memtable != NULL)
      *is_memtable = result;

    ReleaseSysCache(tup);

    return handle;
  }

  if (is_memtable != NULL)
    *is_memtable = false;

  return DSM_HANDLE_INVALID;
}

/*
 * Called after CREATE TABLE or TRUNCATE completes, once the arena's real DSM
 * handle can safely be written to pg_class.relfilenode.
 *
 * This function is a no-op if the relation is not in the cache, which could be
 * the case if it wasn't given a dsm_handle. This makes it safe to call on any
 * relid without having to check whether it uses the memtable access method
 * first.
 *
 * After this, any backend can attach the arena with
 * dsm_attach(rel->rd_locator.relNumber).
 */
static void memtable_finalize_new_relation(Oid relid) {
  MemTableStateCacheEntry* entry = memtable_cache_find(relid);

  if (entry != NULL) {
    dsm_handle handle = dsm_segment_handle(entry->seg);
    Relation pg_class_rel = table_open(RelationRelationId, RowExclusiveLock);
    HeapTuple tup = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relid));

    if (HeapTupleIsValid(tup)) {
      Form_pg_class classForm = (Form_pg_class)GETSTRUCT(tup);

      classForm->relfilenode = (RelFileNumber)handle;
      CatalogTupleUpdate(pg_class_rel, &tup->t_self, tup);
      heap_freetuple(tup);

      CommandCounterIncrement();
      CacheInvalidateRelcacheByRelid(relid);
    }
    table_close(pg_class_rel, RowExclusiveLock);
  }
}

/*
 * The relations whose new arena has to be written to pg_class.relfilenode once
 * the utility statement that created it has finished.
 */
static List* pending_finalize = NIL;

/*
 * Remember that the arena that was just created for relid has to be written
 * to pg_class.relfilenode when the utility statement has finished.
 */
void memtable_schedule_finalize(Oid relid) {
  MemoryContext oldcxt = MemoryContextSwitchTo(TopMemoryContext);

  if (!list_member_oid(pending_finalize, relid))
    pending_finalize = lappend_oid(pending_finalize, relid);
  MemoryContextSwitchTo(oldcxt);
}

/*
 * ProcessUtility hook: finalize the arena's relfilenode once the DDL that
 * replaced it has returned. Needed for two different reasons:
 *
 *   - CREATE TABLE: pg_class doesn't have a row yet when our TAM callback
 *     runs (see memtable_finalize_new_relation's own comment).
 *   - TRUNCATE: pg_class *does* have a row, but RelationSetNewRelfilenumber
 *     (relcache.c) writes its own freshly allocated relfilenumber into it
 *     immediately after calling our callback, clobbering anything we'd try
 *     to write synchronously. We have to overwrite it again afterward, same
 *     as CREATE TABLE, just for a different reason.
 *
 * The statement does not have to be a CREATE TABLE or a TRUNCATE. A table is
 * also created by CREATE TABLE AS, SELECT INTO and CREATE MATERIALIZED VIEW,
 * so the callback that creates the arena schedules the relation, and every
 * relation that was scheduled by the statement is finalized here.
 */

static void memtable_ProcessUtility(PlannedStmt* pstmt, const char* queryString,
                                    bool readOnlyTree,
                                    ProcessUtilityContext context,
                                    ParamListInfo params,
                                    QueryEnvironment* queryEnv,
                                    DestReceiver* dest, QueryCompletion* qc) {
  List* relids;
  ListCell* lc;

  if (prev_ProcessUtility)
    prev_ProcessUtility(pstmt, queryString, readOnlyTree, context, params,
                        queryEnv, dest, qc);
  else
    standard_ProcessUtility(pstmt, queryString, readOnlyTree, context, params,
                            queryEnv, dest, qc);

  /* Take the list first, finalizing a relation can run more statements. */
  relids = pending_finalize;
  pending_finalize = NIL;

  foreach (lc, relids)
    memtable_finalize_new_relation(lfirst_oid(lc));
  list_free(relids);
}

/*
 * Arena lifecycle cleanup -- deferred to transaction end
 *
 * Three different events can mean an arena needs to go away, and all three
 * need the same protection. DROP TABLE can still roll back, so the arena
 * can't be torn down the moment object_access_hook sees the drop; it has
 * to wait for the transaction to actually commit. Symmetrically, if
 * CREATE TABLE itself rolls back, the arena it already created (before the
 * transaction had any chance to fail) needs to be torn down on abort
 * instead. And the *old* arena TRUNCATE replaces needs the same treatment
 * as DROP TABLE: released on commit, left alone if the transaction aborts
 * (see memtable_create_arena). One pending-list/xact-callback mechanism
 * covers all three.
 */

typedef struct MemTablePendingCleanup {
  Oid relid;
  dsm_handle handle;
  XactEvent event;        /* release on this event */
  SubTransactionId subid; /* innermost (sub)transaction that scheduled it */
} MemTablePendingCleanup;

static List* pending_cleanups = NIL;

static void memtable_xact_callback(XactEvent event, void* arg) {
  if (event == XACT_EVENT_COMMIT || event == XACT_EVENT_ABORT) {
    ListCell* lc;
    foreach (lc, pending_cleanups) {
      MemTablePendingCleanup* item = lfirst(lc);

      Assert(item->event == XACT_EVENT_COMMIT ||
             item->event == XACT_EVENT_ABORT);

      if (item->event == event && item->handle != DSM_HANDLE_INVALID)
        memtable_release_arena(item->handle);

      memtable_cache_remove(item->relid);
      pfree(item);
    }
    list_free(pending_cleanups);
    pending_cleanups = NIL;

    /* The statement failed before its relations were finalized. */
    list_free(pending_finalize);
    pending_finalize = NIL;
  }
}

/*
 * A subtransaction that aborts undoes everything scheduled inside it, but the
 * top-level callback above only runs once the whole transaction ends -- by
 * which time the top level may well commit. Without this, a rolled-back
 * DROP TABLE would still release its arena at the top-level commit, and a
 * rolled-back CREATE TABLE or TRUNCATE would leak the arena it created.
 *
 * On abort, resolve the items scheduled in this subtransaction now: release
 * the ones waiting for ABORT, discard the ones waiting for COMMIT. On commit,
 * hand them to the parent, which decides when the top level ends.
 */
static void memtable_subxact_callback(SubXactEvent event,
                                      SubTransactionId mySubid,
                                      SubTransactionId parentSubid, void* arg) {
  ListCell* lc;

  if (event != SUBXACT_EVENT_COMMIT_SUB && event != SUBXACT_EVENT_ABORT_SUB)
    return;

  foreach (lc, pending_cleanups) {
    MemTablePendingCleanup* item = lfirst(lc);

    if (item->subid != mySubid)
      continue;

    if (event == SUBXACT_EVENT_COMMIT_SUB) {
      item->subid = parentSubid;
      continue;
    }

    /*
     * Only an arena being released is what the local state entry points at;
     * an item waiting for COMMIT (DROP, the old arena) leaves it alone.
     */
    if (item->event == XACT_EVENT_ABORT && item->handle != DSM_HANDLE_INVALID) {
      memtable_release_arena(item->handle);
      memtable_cache_remove(item->relid);
    }

    pfree(item);
    pending_cleanups = foreach_delete_current(pending_cleanups, lc);
  }
}

/*
 * Schedule releasing the arena named by handle when the current transaction
 * ends with the given event (COMMIT or ABORT). At that point the segment is
 * unpinned, and this backend's mapping is detached if it has one; the mapping
 * is looked up by handle when the callback runs, since many callers (DROP, the
 * old arena replaced by TRUNCATE) never mapped the arena in this backend. The
 * relid's local state entry is dropped when the transaction ends, whichever
 * event it is. The item is tagged with the current subtransaction so a
 * ROLLBACK TO SAVEPOINT can resolve it early (see memtable_subxact_callback).
 */
void memtable_schedule_cleanup(Oid relid, dsm_handle handle, XactEvent event) {
  MemoryContext oldcxt = MemoryContextSwitchTo(TopMemoryContext);
  MemTablePendingCleanup* item = palloc_object(MemTablePendingCleanup);

  item->relid = relid;
  item->handle = handle;
  item->event = event;
  item->subid = GetCurrentSubTransactionId();

  pending_cleanups = lappend(pending_cleanups, item);
  MemoryContextSwitchTo(oldcxt);
}

/*
 * object_access_hook fires for every dropped object regardless of DDL
 * shape (DROP TABLE, DROP TABLE CASCADE, ...), which is simpler than trying
 * to pick apart DropStmt the way memtable_ProcessUtility picks apart
 * CreateStmt/TruncateStmt.
 */
static object_access_hook_type prev_object_access_hook = NULL;

static void memtable_object_access(ObjectAccessType access, Oid classId,
                                   Oid objectId, int subId, void* arg) {
  if (prev_object_access_hook)
    prev_object_access_hook(access, classId, objectId, subId, arg);

  if (access == OAT_DROP && classId == RelationRelationId && subId == 0) {
    bool uses_memtable;
    dsm_handle handle = memtable_catalog_lookup(objectId, &uses_memtable);

    if (handle != DSM_HANDLE_INVALID && uses_memtable)
      memtable_schedule_cleanup(objectId, handle, XACT_EVENT_COMMIT);
  }
}

/*
 * Module init
 */

void _PG_init(void) {
  if (!process_shared_preload_libraries_in_progress)
    ereport(ERROR,
            (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
             errmsg("memtable must be loaded with shared_preload_libraries")));

  memtable_shmem_init();

  prev_ProcessUtility = ProcessUtility_hook;
  ProcessUtility_hook = memtable_ProcessUtility;

  prev_object_access_hook = object_access_hook;
  object_access_hook = memtable_object_access;

  RegisterXactCallback(memtable_xact_callback, NULL);
  RegisterSubXactCallback(memtable_subxact_callback, NULL);
}
