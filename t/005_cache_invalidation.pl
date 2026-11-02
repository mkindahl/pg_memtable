# A backend lets go of the arena of a table that another backend has
# dropped or truncated, when the next transaction of the backend ends. The
# mappings of the backend are read from /proc/<pid>/maps (Linux).
use strict;
use warnings;

use Cwd qw(abs_path);
use File::Path qw(make_path);
use FindBin;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

plan skip_all => 'needs /proc/<pid>/maps' unless -r '/proc/self/maps';

my $ext_dir = abs_path("$FindBin::RealBin/..");

my $node = PostgreSQL::Test::Cluster->new('cache_invalidation');
$node->init;

# Make the extension in the build directory available without installing it.
my $override_base = abs_path($node->data_dir) . "/ext_override";
make_path("$override_base/extension");
{
    open my $src, '<', "$ext_dir/memtable.control" or die "open control: $!";
    open my $dst, '>', "$override_base/extension/memtable.control"
        or die "open copy: $!";
    print $dst $_ while <$src>;
    print $dst "directory = '$ext_dir'\n";
    print $dst "module_pathname = '$ext_dir/memtable'\n";
}

$node->append_conf('postgresql.conf',
    "shared_preload_libraries = '$ext_dir/memtable'\n");
$node->append_conf('postgresql.conf',
    "extension_control_path = '$override_base:\$system'\n");
$node->start;

$node->safe_psql('postgres', q{
    CREATE EXTENSION memtable;
    CREATE TABLE t_drop  (id int) USING memtable;
    CREATE TABLE t_trunc (id int) USING memtable;
    CREATE TABLE t_other (id int) USING memtable;
    INSERT INTO t_drop  VALUES (1), (2), (3);
    INSERT INTO t_trunc VALUES (1), (2), (3);
    INSERT INTO t_other VALUES (1), (2), (3);
});

my $reader = $node->background_psql('postgres');
my $pid = $reader->query_safe("SELECT pg_backend_pid()");

# The number of arenas that the reader has mapped.
sub mapped
{
    open my $fh, '<', "/proc/$pid/maps" or die "open maps: $!";
    my $n = grep { m{/dev/shm/PostgreSQL\.} } <$fh>;
    return $n;
}

# Run a statement of its own first: the invalidation messages are processed
# when a transaction starts, and the mapping is released when it ends.
sub idle_transaction { $reader->query_safe("SELECT 1"); }

my $base = mapped();

# DROP TABLE in another session
is($reader->query_safe("SELECT count(*) FROM t_drop"), 3, 'reader reads t_drop');
is(mapped(), $base + 1, 'reader has mapped the arena of t_drop');

$node->safe_psql('postgres', "DROP TABLE t_drop");
is(mapped(), $base + 1, 'the arena is still mapped while the reader is idle');

idle_transaction();
is(mapped(), $base, 'the arena is released when the next transaction ends');

# TRUNCATE in another session
is($reader->query_safe("SELECT count(*) FROM t_trunc"), 3, 'reader reads t_trunc');
is(mapped(), $base + 1, 'reader has mapped the arena of t_trunc');

$node->safe_psql('postgres', "TRUNCATE t_trunc");
idle_transaction();
is(mapped(), $base, 'the old arena is released after the TRUNCATE');

is($reader->query_safe("SELECT count(*) FROM t_trunc"), 0,
    'reader sees the truncated table');
is(mapped(), $base + 1, 'reader has mapped the new arena');

# An invalidation that leaves the arena alone releases the mapping too, and
# the next access maps the arena again.
is($reader->query_safe("SELECT count(*) FROM t_other"), 3, 'reader reads t_other');
is(mapped(), $base + 2, 'reader has mapped the arenas of t_trunc and t_other');

$node->safe_psql('postgres', "GRANT SELECT ON t_other TO PUBLIC");
idle_transaction();
is(mapped(), $base + 1, 'the mapping of t_other is released');

is($reader->query_safe("SELECT count(*) FROM t_other"), 3,
    'reader still sees the rows of t_other');
is(mapped(), $base + 2, 'reader has mapped the arena of t_other again');

$reader->quit;
$node->stop;
done_testing();
