#include "bytecode/image.h"
#include "bytecode/interp.h"
#include "bytecode/primitives.h"
#include "driver.h"
#include "gc/gc.h"
#include "logging.h"

Err *gs_run_raw_image(u32 size, const u8 *buf) {
  Image *img;
  GS_TRY(gs_index_image(size, buf, &img));
  GS_TRY(gs_run_image(img));
  GS_RET_OK;
}

Err *gs_run_image(Image *img) {
  GS_TRY(gs_alloc_sym_table());
  GS_TRY(gs_add_primitives());

  GS_FAIL_IF(!img->start.code, "No start function", NULL);
  InterpClosure *start;
  GS_TRY(gs_interp_closure(img, img->start.code - 1, NULL, 0, &start));
  Val ret;

  LOG_DEBUG("%s", "Found start function, running");

  GS_TRY(gs_gc_push_scope());
  PUSH_DIRECT_GC_ROOTS(0, NULL);

  GS_TRY_C(gs_call(&start->parent, 0, NULL, 0, &ret), POP_GC_ROOTS());

  Symbol *main;
  GS_TRY(gs_intern(GS_UTF8_CSTR("main"), &main));
  if (main->value == PTR2VAL_GC(main)) {
    LOG_INFO("%s", "No main function defined");
  } else {
    GS_TRY_C(gs_call(&main->fn, 0, NULL, 1, &ret), POP_GC_ROOTS());
  }

  LOG_DEBUG("%s", "Done, cleaning up");

  POP_GC_ROOTS();
  GS_TRY(gs_gc_pop_scope());

  GS_RET_OK;
}
