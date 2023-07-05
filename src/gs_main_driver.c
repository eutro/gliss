#include "rt.h"
#include "gc/gc.h"
#include "bytecode/primitives.h"

Err *gs_main(void);

int gs_argc;
const char **gs_argv;

static GcAllocator gc;
static Err *gs_main0() {
  GS_TRY(gs_gc_init(GC_DEFAULT_CONFIG, &gc));
  GS_TRY(gs_add_primitive_types());
  GS_TRY(gs_main());
  GS_RET_OK;
}

int main(int argc, const char **argv) {
  gs_argc = argc;
  gs_argv = argv;

  Err *err;
  GS_WITH_ALLOC(&gs_c_alloc) {
    err = gs_main0();
    if (err) {
      gs_write_error(err);
    }
    err = gs_gc_dispose(&gc);
    if (err) {
      gs_write_error(err);
    }
  }

  return err != 0;
}
