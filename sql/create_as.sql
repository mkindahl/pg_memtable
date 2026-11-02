-- A table that is created by CREATE TABLE AS or SELECT INTO can be dropped,
-- like one that is created by CREATE TABLE. The source table is empty, so no
-- rows are copied.
CREATE TABLE t_src (id int, name text);

CREATE TABLE t_ctas USING memtable AS SELECT * FROM t_src;

SET default_table_access_method = memtable;
SELECT * INTO t_into FROM t_src;
RESET default_table_access_method;

SELECT c.relname, a.amname
  FROM pg_class c JOIN pg_am a ON a.oid = c.relam
 WHERE c.relname IN ('t_ctas', 't_into')
 ORDER BY c.relname;

DROP TABLE t_ctas, t_into;
DROP TABLE t_src;
