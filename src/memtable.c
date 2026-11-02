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
static dsm_handle memtable_catalog_lookup(Oid relid, bool* is_memtable) {
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
 * Called after CREATE TABLE completes, once the arena's real DSM
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

    memtable_cache_remove(relid); /* the handle is all that is needed now */

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
 * created it has returned. pg_class doesn't have a row yet when our TAM
 * callback runs (see memtable_finalize_new_relation's own comment).
 *
 * The statement does not have to be a CREATE TABLE. A table is
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
 * object_access_hook fires for every dropped object regardless of DDL
 * shape (DROP TABLE, DROP TABLE CASCADE, ...), which is simpler than trying
 * to pick apart DropStmt the way memtable_ProcessUtility picks apart
 * CreateStmt.
 *
 * The arena is released right away here, which is only correct if the
 * transaction commits.
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
      memtable_release_arena(handle);
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
}
