#include "rt.h"

Err *gs_main(void);

int main() {
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
