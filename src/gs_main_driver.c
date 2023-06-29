#include "rt.h"
#include "gc/gc.h"
#include "bytecode/primitives.h"

Err *gs_main(void);

int gs_argc;
const char **gs_argv;

static Err *gs_main0() {
  GcAllocator gc;
  GS_TRY(gs_gc_init(GC_DEFAULT_CONFIG, &gc));
  GS_TRY(gs_add_primitive_types());
  GS_TRY(gs_main());
  GS_TRY(gs_gc_dispose(&gc));
  GS_RET_OK;
}

int main(int argc, const char **argv) {
  gs_argc = argc;
  gs_argv = argv;

  Err *err;
  GS_WITH_ALLOC(&gs_c_alloc) {
    err = gs_main0();
  }
  if (err) {
    GS_FILE_OUTSTREAM(gs_stderr, stderr);
    err = gs_write_error(err, &gs_stderr);
    if (err) return 2;
    return 1;
  }
}
