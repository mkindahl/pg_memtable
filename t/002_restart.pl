# A restart of the server destroys every arena, but pg_class still has the
# handles of them. A table from before the restart can be dropped.
use strict;
use warnings;

use Cwd qw(abs_path);
use File::Path qw(make_path);
use FindBin;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

my $ext_dir = abs_path("$FindBin::RealBin/..");

my $node = PostgreSQL::Test::Cluster->new('restart');
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
    CREATE TABLE t_before (id int) USING memtable;
    CREATE TABLE t_before2 (id int) USING memtable;
});

$node->restart;

my ($ret, $stdout, $stderr) =
    $node->psql('postgres', 'DROP TABLE t_before');
is($ret, 0, 'a table from before the restart can be dropped') or diag $stderr;

$node->safe_psql('postgres', 'CREATE TABLE t_after (id int) USING memtable');
$node->safe_psql('postgres', 'DROP TABLE t_after, t_before2');
is($node->safe_psql('postgres', "SELECT count(*) FROM pg_class WHERE relname IN ('t_before', 't_before2', 't_after')"),
    0, 'the tables are gone and the server is still up');

$node->stop;
done_testing();
