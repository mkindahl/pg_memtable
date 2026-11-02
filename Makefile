# memtable: a table access method that keeps the rows of a table in shared
# memory.
#
#   make                  build the module
#   make install          install it
#   make installcheck     run the regression, isolation and TAP tests against a
#                         running server. The extension has to be installed and
#                         the server needs shared_preload_libraries = 'memtable'.
#                         The tests have to run as a superuser.
#   make check-temp       build the module, start a temporary server that loads
#                         it from this directory, and run the same tests there,
#                         without installing anything
#
# The tests arena, transactions and truncate count the shared memory segments
# that the backend has mapped by reading /proc/self/maps, so they only run on
# Linux.

MODULE_big = memtable
OBJS = src/memtable.o src/memtable_shmem.o src/memtable_cache.o src/memtable_handler.o
PG_CPPFLAGS = -Isrc

EXTENSION = memtable
DATA = memtable--0.1.sql
PGFILEDESC = "memtable - table access method that keeps rows in shared memory"

REGRESS = create_table create_as unsupported insert_select scan toast arena_full
ifeq ($(shell uname -s),Linux)
REGRESS += arena transactions truncate
endif
REGRESS_OPTS = --load-extension=memtable
ISOLATION = visibility create_as_other_session
ISOLATION_OPTS = --load-extension=memtable
TAP_TESTS = 1

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

src/memtable.o: src/memtable.c src/memtable.h src/memtable_shmem.h src/memtable_cache.h
src/memtable_shmem.o: src/memtable_shmem.c src/memtable_shmem.h src/memtable_cache.h src/memtable.h
src/memtable_cache.o: src/memtable_cache.c src/memtable_cache.h src/memtable_shmem.h src/memtable.h
src/memtable_handler.o: src/memtable_handler.c src/memtable_shmem.h src/memtable_cache.h src/memtable.h

# PGXS does not support "make check", so start a temporary instance that
# preloads the module from this directory and has the extension files in
# its own extension directory.
TEMP_DIR = $(CURDIR)/tmp_instance

.PHONY: check-temp
check-temp: all
	rm -rf '$(TEMP_DIR)'
	mkdir -p '$(TEMP_DIR)/ext/extension'
	cp memtable.control $(DATA) '$(TEMP_DIR)/ext/extension/'
	echo "shared_preload_libraries = '$(CURDIR)/memtable'" > '$(TEMP_DIR)/temp.conf'
	echo "extension_control_path = '$(TEMP_DIR)/ext:\$$system'" >> '$(TEMP_DIR)/temp.conf'
	echo "dynamic_library_path = '$(CURDIR):\$$libdir'" >> '$(TEMP_DIR)/temp.conf'
	$(pg_regress_installcheck) --temp-instance='$(TEMP_DIR)/instance' --temp-config='$(TEMP_DIR)/temp.conf' $(REGRESS_OPTS) $(REGRESS)
	$(pg_isolation_regress_installcheck) --temp-instance='$(TEMP_DIR)/instance_iso' --temp-config='$(TEMP_DIR)/temp.conf' $(ISOLATION_OPTS) $(ISOLATION)
	$(prove_installcheck)
