# Limitations

This is what `memtable` does not do, as far as it is known. Some of it is by design, and some of it is work that has not been done yet.

## Data and durability

- The rows live in shared memory and are not written to the write-ahead log. A restart of the server loses all of them, and a table from before the restart cannot be read or inserted into. It can be dropped, or truncated, which gives it a new arena.
- The size of an arena is fixed when the table is created (`memtable.arena_size`, 64 MB by default). A statement that does not fit fails with an error, and the only way to get more room is to create the table again with a larger setting. Space is never reused, since there is no `DELETE`, `UPDATE` or vacuum.
- Logged and unlogged tables behave the same way, since neither is written to the log. Temporary tables are rejected.
- The module has to be loaded with `shared_preload_libraries`.

## SQL

- There is no `DELETE`, `UPDATE`, `SELECT ... FOR UPDATE`, `INSERT ... ON CONFLICT`, index or primary key. The only way to find a row is to read all of them.
- A scan only goes forward. `FETCH BACKWARD` on a scrollable cursor raises an error. Bitmap scans, TID scans, `TABLESAMPLE` scans and parallel scans are not supported.
- `ANALYZE` fails, and so does the analyze that autovacuum starts, because the callbacks that it needs are not implemented. The planner estimates that a table has no rows.
- A command that rewrites a table, such as `ALTER TABLE ... SET ACCESS METHOD`, `ALTER TABLE ... ALTER COLUMN ... TYPE`, `REFRESH MATERIALIZED VIEW`, `CLUSTER` and `VACUUM FULL`, raises an error, and the table is left as it was.
- Values that are stored out of line in another table are copied into the arena when they are inserted. There is no TOAST table for a `memtable` table.

## Transactions

- A row is visible when the transaction that inserted it has committed, or when it is the current transaction. The snapshot of the statement is not used, so a statement that runs while another transaction commits can see some of its rows and not others, and a transaction does not get a repeatable read.
- The command id is not used. A row is visible to the rest of its own transaction as soon as it has been inserted. A scan stops where the arena ended when it started, so a statement does not see its own rows.
- `PREPARE TRANSACTION` is not supported after a table has been created, dropped or truncated in the transaction. The arenas that the transaction scheduled for release are forgotten, so they are not released, or are released too early.
- The cleanup of arenas is only done when the transaction of the leader ends. A parallel worker that creates or drops a table is not handled, which cannot happen with the commands that are supported.
