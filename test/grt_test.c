#include "bytecode/image.h"
#include "gc/gc.h"
#include "driver.h"

#include "util/io.h"

Err *gs_main() {
  AllocMeta allocMeta;
  u8 *buf;
  GS_TRY(gs_read_file("grt_gi.gi", &allocMeta, &buf));
  GS_TRY(gs_run_raw_image(allocMeta.count, buf));
  gs_free(buf, allocMeta);
  gs_gc_dump();

  GS_RET_OK;
}
