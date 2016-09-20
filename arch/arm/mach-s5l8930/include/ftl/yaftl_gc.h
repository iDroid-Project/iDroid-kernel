#ifndef FTL_YAFTL_GC_H
#define FTL_YAFTL_GC_H

#include <ftl/yaftl_common.h>

int gcInit(void);

void gcResetReadCache(GCReadC* _readC);

void gcListPushBack(GCList* _gcList, uint32_t _block);

void gcFreeBlock(uint32_t _block, uint8_t _scrub);

void gcPrepareToWrite(uint32_t _numPages);

void gcFreeIndexPages(uint32_t _victim, uint8_t _scrub);

void gcPrepareToFlush(void);

#endif // FTL_YAFTL_GC_H
