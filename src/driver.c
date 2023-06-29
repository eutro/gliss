#include "bytecode/image.h"
#include "bytecode/interp.h"
#include "bytecode/primitives.h"
#include "driver.h"
#include "gc/gc.h"
#include "logging.h"

Err *gs_run_raw_image(u32 size, const u8 *buf) {
  Image img;
  GS_TRY(gs_index_image(size, buf, &img));
  GS_TRY(gs_run_image(&img));
  gs_free_image(&img);
  GS_RET_OK;
}

Err *gs_run_image(Image *img) {
  GcAllocator gc;
  GS_TRY(gs_gc_init(GC_DEFAULT_CONFIG, &gc));

  SymTable *table = gs_alloc_sym_table();
  GS_TRY(gs_add_primitives(table));
  gs_global_syms = table;

  GS_FAIL_IF(!img->start.code, "No start function", NULL);
  InterpClosure start = gs_interp_closure(img, img->start.code - 1);
  Val ret;

  LOG_DEBUG("%s", "Found start function, running");

  GS_TRY(gs_gc_push_scope());
  PUSH_DIRECT_GC_ROOTS(0, NULL);

  GS_TRY_C(gs_call((Closure *) &start, 0, NULL, 1, &ret), POP_GC_ROOTS());
  LOG_DEBUG("%s", "Done, cleaning up");

  POP_GC_ROOTS();
  GS_TRY(gs_gc_pop_scope());

  gs_free_sym_table(table);
  GS_TRY(gs_gc_dispose(&gc));

  GS_RET_OK;
}
