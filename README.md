# memtable

`memtable` is a PostgreSQL table access method that keeps the rows of a table in shared memory instead of in the heap. It is built step by step in the series [Building a Table Access Method](https://dbmsdrops.kindahl.net/), and each commit on this branch adds the code for one post of the series.

## The posts

The posts that go with the code up to this commit:

- [Part 1: TAM vs. FDW](https://dbmsdrops.kindahl.net/2026/09/06/table-access-methods-part-1/)
- [Part 2: the minimal TAM skeleton](https://dbmsdrops.kindahl.net/2026/09/13/table-access-method-part-2/)
- [Part 3: shared memory in Postgres](https://dbmsdrops.kindahl.net/2026/09/27/building-a-table-access-method-part-3/)
- [Part 4: creating and dropping the arena](https://dbmsdrops.kindahl.net/2026/10/11/building-a-table-access-method-part-4/)
- [Part 5: commit, abort, and savepoints](https://dbmsdrops.kindahl.net/2026/10/16/building-a-table-access-method-part-5/)
- [Part 6: TRUNCATE](https://dbmsdrops.kindahl.net/2026/10/25/building-a-table-access-method-part-6/)

## What it does so far

- The callbacks of the table access method exist, and `CREATE TABLE ... USING memtable` and `DROP TABLE` work. Every other callback raises an error that says it is not implemented yet.
- Creating a table, with `CREATE TABLE`, `CREATE TABLE AS` or `SELECT INTO`, creates an arena, a shared memory (DSM) segment that will hold the rows of the table. Its handle is stored in `pg_class.relfilenode`, so every backend can attach to it. Dropping the table releases the arena. Temporary tables, and commands that rewrite a table, are rejected with an error.
- The arena follows the transaction: a rolled-back `CREATE TABLE` releases the arena it created, a rolled-back `DROP TABLE` keeps it, and the same holds for savepoints.
- `TRUNCATE` gives the table a new arena, and a rolled-back `TRUNCATE` leaves the old one in place.
- Rows can be inserted and read back with a full table scan, from any backend. Rows are stored in the heap tuple format, a TID is the offset of the row in the arena, and a row is visible once the transaction that inserted it has committed. Backward scans are not supported.

Not done yet:

- There are no `DELETE`, `UPDATE`, index or primary key yet, so the only way to find a row is to read all of them.
- The scan only goes forward.
- Nothing survives a server restart. The arenas are gone but the tables are still in the catalog, so reading or inserting into such a table fails. It can be dropped, or truncated, which gives it a new arena.

## Build and install

You need PostgreSQL with the server headers and `pg_config` on the `PATH`. The code is developed and tested against PostgreSQL 18.

```
make
sudo make install
```

Add the module to `shared_preload_libraries` in `postgresql.conf` and restart the server:

```
shared_preload_libraries = 'memtable'
```

Then create the extension in a database and use the access method:

```sql
CREATE EXTENSION memtable;
CREATE TABLE t (id bigint, name text) USING memtable;
```

## Tests

```
make installcheck
make check-temp
```

`make installcheck` runs the tests against a running server. The extension has to be installed, the server has to preload the module, and the tests have to run as a superuser. `make check-temp` starts a temporary server that loads the module from this directory and runs the same tests, so nothing has to be installed.

The tests:

- `create_table`: a table can be created and dropped.
- `create_as` and `unsupported`: tables created by `CREATE TABLE AS` and `SELECT INTO`, and the temporary tables and rewrites that are rejected.
- `arena`: the arena of a table is created and released (Linux only).
- `t/001_preload.pl` and `t/002_restart.pl` (TAP tests): the module has to be preloaded, and a table from before a restart can be dropped.
- `transactions`: the arena follows commit, abort and savepoints (Linux only).
- `truncate`: `TRUNCATE` and its rollback (Linux only).
- `t/003_restart_truncate.pl` (TAP test): a table from before a restart can be truncated.
- `insert_select`, `scan`, `toast` and `arena_full`: inserting and reading rows, the scan callbacks, values that are stored out of line in another table, and a full arena.
- `visibility` and `create_as_other_session` (isolation tests): a row is hidden from other sessions until its transaction commits, and a table created by `CREATE TABLE AS` can be used by them.
- `t/004_concurrent.pl` (TAP test): inserts and scans that run at the same time.

The tests that are marked Linux only count the shared memory segments that the backend has mapped by reading `/proc/<pid>/maps`.

## Layout

| Path | Contents |
| ---- | -------- |
| `src/memtable.c`, `src/memtable.h` | The transaction and DDL hooks, the tuple visibility test, and `_PG_init` |
| `src/memtable_shmem.c`, `src/memtable_shmem.h` | The arenas: the shared memory segments that hold the rows, and the allocator inside them |
| `src/memtable_cache.c`, `src/memtable_cache.h` | The cache of the arenas that this backend has attached, and its invalidation |
| `src/memtable_handler.c` | The callbacks of the table access method |
| `memtable.control`, `memtable--0.1.sql` | The extension |
| `sql/`, `expected/` | Regression tests |
| `specs/` | Isolation tests |
| `t/` | TAP tests |
