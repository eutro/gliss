#include "gc/gc.h"

Err *gs_main() {
  GcAllocator gc;
  GS_TRY(gs_gc_init(GC_DEFAULT_CONFIG, &gc));

  

  GS_TRY(gs_gc_dispose(&gc));

  GS_RET_OK;
}
