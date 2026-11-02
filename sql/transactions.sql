-- The arena follows the transaction. A rolled-back CREATE TABLE
-- releases the arena it created, a rolled-back DROP TABLE keeps it, and the
-- same holds for savepoints.
CREATE FUNCTION pg_temp.mapped_arenas() RETURNS int LANGUAGE sql AS $$
  SELECT count(*)::int
    FROM regexp_split_to_table(pg_read_file('/proc/self/maps'), E'\n') AS l
   WHERE l LIKE '%/dev/shm/PostgreSQL.%'
$$;

SELECT pg_temp.mapped_arenas() AS base \gset

-- CREATE TABLE that rolls back
BEGIN;
CREATE TABLE t_tx (id int) USING memtable;
SELECT pg_temp.mapped_arenas() - :base AS inside_transaction;
ROLLBACK;
SELECT to_regclass('t_tx') IS NULL AS table_gone;
SELECT pg_temp.mapped_arenas() - :base AS arenas;

CREATE TABLE t_tx (id int) USING memtable;
SELECT pg_temp.mapped_arenas() - :base AS arenas_after_create;

-- DROP TABLE that rolls back
BEGIN;
DROP TABLE t_tx;
ROLLBACK;
SELECT to_regclass('t_tx') IS NOT NULL AS table_kept;
SELECT pg_temp.mapped_arenas() - :base AS arenas_after_rolled_back_drop;

-- savepoints
BEGIN;
SAVEPOINT s;
CREATE TABLE t_sp (id int) USING memtable;
ROLLBACK TO s;
COMMIT;
SELECT to_regclass('t_sp') IS NULL AS savepoint_create_gone;
SELECT pg_temp.mapped_arenas() - :base AS arenas_after_savepoint_create;

BEGIN;
SAVEPOINT s;
DROP TABLE t_tx;
ROLLBACK TO s;
COMMIT;
SELECT to_regclass('t_tx') IS NOT NULL AS savepoint_drop_kept;
SELECT pg_temp.mapped_arenas() - :base AS arenas_after_savepoint_drop;

BEGIN;
SAVEPOINT s;
DROP TABLE t_tx;
RELEASE s;
COMMIT;
SELECT to_regclass('t_tx') IS NULL AS released_drop_applied;
SELECT pg_temp.mapped_arenas() - :base AS arenas_at_end;
