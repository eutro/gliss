#include <stdio.h>
#include "../rt.h"

static Err *gs_read_file(const char *fname, AllocMeta *meta, u8 **out) {
  u32 size = 1024;
  AllocMeta allocMeta = GS_ALLOC_META(u8, size);
  allocMeta.align = alignof(u32);
  u8 *buf = gs_alloc(allocMeta);
  GS_FAIL_IF(!buf, "Could not allocate buffer", NULL);

  FILE *file = fopen(fname, "r");
  GS_FAIL_IF(!file, "Could not open file", NULL);

  u32 offset = 0;
  while (true) {
    size_t count = size - offset;
    size_t read = fread(buf + offset, 1, count, file);
    offset += read;
    if (feof(file)) break;
    if (read != count) {
      if (ferror(file)) {
        fclose(file);
        GS_FAILWITH("Error reading file", NULL);
      }
      continue;
    }
    AllocMeta oldAllocMeta = allocMeta;
    size = allocMeta.count *= 2;
    buf = gs_realloc(buf, oldAllocMeta, allocMeta);
    if (!buf) {
      fclose(file);
      GS_FAILWITH("Could not reallocate buffer", NULL);
    }
  }
  AllocMeta oldAllocMeta = allocMeta;
  allocMeta.count = offset;
  buf = gs_realloc(buf, oldAllocMeta, allocMeta);
  fclose(file);
  if (!buf) {
    GS_FAILWITH("Could not trim buffer", NULL);
  }

  *meta = allocMeta;
  *out = buf;

  GS_RET_OK;
}
