# memtable

`memtable` is a PostgreSQL table access method that keeps the rows of a table in shared memory instead of in the heap. It is built step by step in the series [Building a Table Access Method](https://dbmsdrops.kindahl.net/), and each commit on this branch adds the code for one post of the series.

## The posts

The posts that go with the code up to this commit:

- [Part 1: TAM vs. FDW](https://dbmsdrops.kindahl.net/2026/09/06/table-access-methods-part-1/)
- [Part 2: the minimal TAM skeleton](https://dbmsdrops.kindahl.net/2026/09/13/table-access-method-part-2/)

## What it does so far

- The callbacks of the table access method exist, and `CREATE TABLE ... USING memtable` and `DROP TABLE` work. Every other callback raises an error that says it is not implemented yet.

Not done yet:

- Rows cannot be stored yet, so there is nothing to insert or read.

## Build and install

You need PostgreSQL with the server headers and `pg_config` on the `PATH`. The code is developed and tested against PostgreSQL 18.

```
make
sudo make install
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

The tests that are marked Linux only count the shared memory segments that the backend has mapped by reading `/proc/<pid>/maps`.

## Layout

| Path | Contents |
| ---- | -------- |
| `src/memtable.c`, `src/memtable.h` | The transaction and DDL hooks, the tuple visibility test, and `_PG_init` |
| `src/memtable_handler.c` | The callbacks of the table access method |
| `memtable.control`, `memtable--0.1.sql` | The extension |
| `sql/`, `expected/` | Regression tests |
| `specs/` | Isolation tests |
| `t/` | TAP tests |
