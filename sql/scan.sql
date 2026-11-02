-- The scan callbacks.
CREATE TABLE t_scan (id int) USING memtable;
INSERT INTO t_scan SELECT g FROM generate_series(1, 5) g;

-- scan_rescan: the inner side of a nested loop is read once per outer row
SET enable_hashjoin = off;
SET enable_mergejoin = off;
SET enable_material = off;
SELECT count(*) FROM (VALUES (1), (2), (3)) v(x) JOIN t_scan ON t_scan.id >= v.x;
RESET enable_hashjoin;
RESET enable_mergejoin;
RESET enable_material;

-- a scrollable cursor can start over, but backward scans are not supported
BEGIN;
DECLARE c SCROLL CURSOR FOR SELECT id FROM t_scan ORDER BY id;
FETCH FORWARD 3 FROM c;
FETCH ABSOLUTE 1 FROM c;
\set ON_ERROR_STOP 0
DECLARE b SCROLL CURSOR FOR SELECT id FROM t_scan;
FETCH FORWARD 2 FROM b;
FETCH BACKWARD 1 FROM b;
\set ON_ERROR_STOP 1
ROLLBACK;

DROP TABLE t_scan;
