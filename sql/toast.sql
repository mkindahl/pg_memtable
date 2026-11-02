-- Values that the executor hands over as pointers into the TOAST
-- table of another table are copied into the arena.
CREATE TABLE t_toast_src (id int, v text, w text);
ALTER TABLE t_toast_src ALTER COLUMN v SET STORAGE EXTERNAL;
INSERT INTO t_toast_src
  SELECT 1, string_agg(md5(g::text), ''), 'short' FROM generate_series(1, 300) g;
INSERT INTO t_toast_src SELECT 2, repeat('abc', 5000), NULL;
INSERT INTO t_toast_src VALUES (3, NULL, 'x');

SELECT pg_relation_size(reltoastrelid) > 0 AS source_has_toast
  FROM pg_class WHERE relname = 't_toast_src';

CREATE TABLE t_toast (id int, v text, w text) USING memtable;
INSERT INTO t_toast SELECT * FROM t_toast_src;

DROP TABLE t_toast_src;

SELECT id, length(v), md5(v), w FROM t_toast ORDER BY id;
DROP TABLE t_toast;
