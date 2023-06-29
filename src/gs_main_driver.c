#include "rt.h"

Err *gs_main(void);

int gs_argc;
const char **gs_argv;

int main(int argc, const char **argv) {
  gs_argc = argc;
  gs_argv = argv;

  Err *err;
  GS_WITH_ALLOC(&gs_c_alloc) {
    err = gs_main();
  }
  if (err) {
    GS_FILE_OUTSTREAM(gs_stderr, stderr);
    err = gs_write_error(err, &gs_stderr);
    if (err) return 2;
    return 1;
  }
}
