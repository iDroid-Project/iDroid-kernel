#ifndef FTL_YAFTL_MEM_H
#define FTL_YAFTL_MEM_H

typedef struct
{
  uint32_t buffer;
  uint32_t endOfBuffer;
  uint32_t size;
  uint32_t numAllocs;
  uint32_t numRebases;
  uint32_t paddingsSize;
  uint32_t state;
} bufzone_t;

// TODO: documentation
extern void* yaftl_alloc(size_t size);
extern void bufzone_init(bufzone_t* _zone);
extern void* bufzone_alloc(bufzone_t* _zone, size_t size);
extern error_t bufzone_finished_allocs(bufzone_t* _zone);
extern void* bufzone_rebase(bufzone_t* _zone, void* buf);
extern error_t bufzone_finished_rebases(bufzone_t* _zone);
#endif // FTL_YAFTL_MEM_H
