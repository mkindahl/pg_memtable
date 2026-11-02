-- TRUNCATE gives the table a new arena, and a rolled-back TRUNCATE
-- leaves the old one in place.
CREATE FUNCTION pg_temp.mapped_arenas() RETURNS int LANGUAGE sql AS $$
  SELECT count(*)::int
    FROM regexp_split_to_table(pg_read_file('/proc/self/maps'), E'\n') AS l
   WHERE l LIKE '%/dev/shm/PostgreSQL.%'
$$;

SELECT pg_temp.mapped_arenas() AS base \gset

CREATE TABLE t_trunc (id int) USING memtable;
SELECT relfilenode AS handle1 FROM pg_class WHERE relname = 't_trunc' \gset
SELECT pg_temp.mapped_arenas() - :base AS arenas_after_create;

TRUNCATE t_trunc;
SELECT relfilenode <> :handle1 AS new_arena FROM pg_class WHERE relname = 't_trunc';
SELECT pg_temp.mapped_arenas() - :base AS arenas_after_truncate;

SELECT relfilenode AS handle2 FROM pg_class WHERE relname = 't_trunc' \gset
BEGIN;
TRUNCATE t_trunc;
ROLLBACK;
SELECT relfilenode = :handle2 AS old_arena_kept FROM pg_class WHERE relname = 't_trunc';
SELECT pg_temp.mapped_arenas() - :base AS arenas_after_rollback;

BEGIN;
SAVEPOINT s;
TRUNCATE t_trunc;
ROLLBACK TO s;
TRUNCATE t_trunc;
COMMIT;
SELECT relfilenode <> :handle2 AS truncated_after_savepoint FROM pg_class WHERE relname = 't_trunc';
SELECT pg_temp.mapped_arenas() - :base AS arenas_at_end;

DROP TABLE t_trunc;
SELECT pg_temp.mapped_arenas() - :base AS arenas_after_drop;
