# The module keeps its state in shared memory, so it has to be loaded when the
# server starts. Loading it in a session fails with an error that says so.
use strict;
use warnings;

use Cwd qw(abs_path);
use File::Path qw(make_path);
use FindBin;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

my $ext_dir = abs_path("$FindBin::RealBin/..");

my $node = PostgreSQL::Test::Cluster->new('preload');
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
    "extension_control_path = '$override_base:\$system'\n");
$node->start;

my ($ret, $stdout, $stderr) =
    $node->psql('postgres', 'CREATE EXTENSION memtable');
isnt($ret, 0, 'the extension cannot be created without shared_preload_libraries');
like($stderr, qr/shared_preload_libraries/, 'the error says that the module has to be preloaded');

$node->stop;
done_testing();
