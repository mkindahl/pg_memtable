# memtable

`memtable` is a PostgreSQL table access method that keeps the rows of a table in shared memory instead of in the heap. It is built step by step in the series [Building a Table Access Method](https://dbmsdrops.kindahl.net/), and each commit on this branch adds the code for one post of the series.

## The posts

The posts that go with the code up to this commit:

- [Part 1: TAM vs. FDW](https://dbmsdrops.kindahl.net/2026/09/06/table-access-methods-part-1/)
- [Part 2: the minimal TAM skeleton](https://dbmsdrops.kindahl.net/2026/09/13/table-access-method-part-2/)
- [Part 3: shared memory in Postgres](https://dbmsdrops.kindahl.net/2026/09/27/building-a-table-access-method-part-3/)
- [Part 4: creating and dropping the arena](https://dbmsdrops.kindahl.net/2026/10/11/building-a-table-access-method-part-4/)

## What it does so far

- The callbacks of the table access method exist, and `CREATE TABLE ... USING memtable` and `DROP TABLE` work. Every other callback raises an error that says it is not implemented yet.
- Creating a table, with `CREATE TABLE`, `CREATE TABLE AS` or `SELECT INTO`, creates an arena, a shared memory (DSM) segment that will hold the rows of the table. Its handle is stored in `pg_class.relfilenode`, so every backend can attach to it. Dropping the table releases the arena. Temporary tables, and commands that rewrite a table, are rejected with an error.

Not done yet:

- Rows cannot be stored yet, so there is nothing to insert or read.
- Nothing survives a server restart. The arenas are gone but the tables are still in the catalog. Such a table can be dropped.

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
