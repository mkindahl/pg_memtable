-- Rows can be inserted and read back.
CREATE TABLE t_rows (id bigint, name text) USING memtable;

INSERT INTO t_rows VALUES (1, 'one'), (2, 'two');
INSERT INTO t_rows VALUES (3, NULL);
SELECT id, name FROM t_rows ORDER BY id;
SELECT count(*) FROM t_rows;

-- a row is visible to its own transaction, and a rolled-back row to nobody
BEGIN;
INSERT INTO t_rows VALUES (4, 'four');
SELECT count(*) FROM t_rows;
ROLLBACK;
SELECT count(*) FROM t_rows;

-- a statement does not see the rows it inserts itself
INSERT INTO t_rows SELECT id + 10, name FROM t_rows;
SELECT count(*) FROM t_rows;
INSERT INTO t_rows SELECT * FROM t_rows;
SELECT count(*) FROM t_rows;

-- TRUNCATE gives the table a new arena, and a rollback brings the old rows back
BEGIN;
TRUNCATE t_rows;
INSERT INTO t_rows VALUES (100, 'new');
SELECT count(*) FROM t_rows;
ROLLBACK;
SELECT count(*) FROM t_rows;

-- the TID of a row is its offset in the arena
CREATE TABLE t_tid (id int) USING memtable;
INSERT INTO t_tid VALUES (1), (2), (3);
SELECT ctid, id FROM t_tid ORDER BY id;

-- values of different sizes
CREATE TABLE t_sizes (id int, v text) USING memtable;
INSERT INTO t_sizes VALUES (1, ''), (2, repeat('a', 126)), (3, repeat('b', 127)), (4, repeat('c', 100000));
SELECT id, length(v), md5(v) FROM t_sizes ORDER BY id;

DROP TABLE t_rows, t_tid, t_sizes;
