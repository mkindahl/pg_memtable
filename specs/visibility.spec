# A row is hidden from other sessions until its transaction commits,
# and stays hidden if the transaction rolls back.

setup
{
    CREATE TABLE t_vis (id int, name text) USING memtable;
    INSERT INTO t_vis VALUES (1, 'one'), (2, 'two');
}

teardown
{
    DROP TABLE t_vis;
}

session s1
step s1_begin  { BEGIN; }
step s1_insert { INSERT INTO t_vis VALUES (10, 'ten'); }
step s1_count  { SELECT count(*) FROM t_vis; }
step s1_commit { COMMIT; }
step s1_abort  { ROLLBACK; }

session s2
step s2_count  { SELECT count(*) FROM t_vis; }

permutation s1_begin s1_insert s1_count s2_count s1_commit s2_count
permutation s1_begin s1_insert s2_count s1_abort s2_count
