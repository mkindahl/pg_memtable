#ifndef MEMTABLE_H
#define MEMTABLE_H

#include <postgres.h>
#include <fmgr.h>

#include <access/heapam.h>
#include <access/multixact.h>
#include <access/tableam.h>
#include <utils/rel.h>

#define NOT_IMPLEMENTED                            \
  ereport(ERROR,                                   \
          (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), \
           errmsg("memtable callback %s not yet implemented", __func__)))

#endif /* MEMTABLE_H */
