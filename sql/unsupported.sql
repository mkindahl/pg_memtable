-- Temporary tables and commands that rewrite a table are rejected, and the
-- table is left as it was.
CREATE TABLE t_heap (id int, name text);
INSERT INTO t_heap VALUES (1, 'one'), (2, 'two');

\set ON_ERROR_STOP 0
CREATE TEMP TABLE t_temp (id int) USING memtable;
ALTER TABLE t_heap SET ACCESS METHOD memtable;
CREATE MATERIALIZED VIEW mv_rewrite USING memtable AS SELECT * FROM t_heap;
\set ON_ERROR_STOP 1

CREATE TABLE t_mem (id int) USING memtable;
\set ON_ERROR_STOP 0
ALTER TABLE t_mem ALTER COLUMN id TYPE bigint;
\set ON_ERROR_STOP 1

-- WITH NO DATA does not rewrite anything, but REFRESH does
CREATE MATERIALIZED VIEW mv_empty USING memtable AS SELECT * FROM t_heap WITH NO DATA;
\set ON_ERROR_STOP 0
REFRESH MATERIALIZED VIEW mv_empty;
\set ON_ERROR_STOP 1

SELECT count(*) FROM t_heap;
SELECT c.relname, a.amname
  FROM pg_class c JOIN pg_am a ON a.oid = c.relam
 WHERE c.relname IN ('t_heap', 't_mem', 'mv_empty')
 ORDER BY c.relname;

DROP MATERIALIZED VIEW mv_empty;
DROP TABLE t_mem, t_heap;
