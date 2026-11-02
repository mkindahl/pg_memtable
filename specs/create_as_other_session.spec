# A table that is created by CREATE TABLE AS or SELECT INTO can be used by
# other sessions, and can be dropped by them, like one that is created by
# CREATE TABLE.

setup
{
    CREATE TABLE t_src (id int, name text);
    INSERT INTO t_src VALUES (1, 'one'), (2, 'two'), (3, 'three');
}

teardown
{
    DROP TABLE t_src;
}

session s1
step s1_ctas { CREATE TABLE t_ctas USING memtable AS SELECT * FROM t_src; }
step s1_into {
    SET default_table_access_method = memtable;
    SELECT * INTO t_into FROM t_src;
}

session s2
step s2_ctas_count { SELECT count(*) FROM t_ctas; }
step s2_ctas_drop  { DROP TABLE t_ctas; }
step s2_into_count { SELECT count(*) FROM t_into; }
step s2_into_drop  { DROP TABLE t_into; }

permutation s1_ctas s2_ctas_count s2_ctas_drop
permutation s1_into s2_into_count s2_into_drop
