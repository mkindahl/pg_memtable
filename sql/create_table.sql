-- A table can be created and dropped with the memtable access method.
CREATE TABLE t_create (id int, name text) USING memtable;

SELECT c.relname, a.amname
  FROM pg_class c JOIN pg_am a ON a.oid = c.relam
 WHERE c.relname = 't_create';

DROP TABLE t_create;
SELECT to_regclass('t_create') IS NULL AS dropped;
