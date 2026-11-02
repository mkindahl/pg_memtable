# A scan that runs while other sessions insert rows must not see a row that
# is not completely written.
use strict;
use warnings;

use Cwd qw(abs_path);
use File::Path qw(make_path);
use FindBin;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

my $ext_dir = abs_path("$FindBin::RealBin/..");

my $node = PostgreSQL::Test::Cluster->new('concurrent');
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
    CREATE TABLE t_stress (id bigint, v text) USING memtable;
});

$node->pgbench(
    '--no-vacuum --client=4 --jobs=2 --time=3',
    0,
    [qr{number of transactions actually processed}],
    [qr{^$}],
    'concurrent inserts and scans',
    {
        '004_insert' => q{INSERT INTO t_stress SELECT g, repeat('x', 1 + g % 100) FROM generate_series(1, 20) g;},
        '004_scan'   => q{SELECT count(*), sum(length(v)) FROM t_stress;},
    });

is($node->safe_psql('postgres', 'SELECT count(*) % 20 FROM t_stress'),
    0, 'every insert is there, and all of it');

$node->stop;
done_testing();
