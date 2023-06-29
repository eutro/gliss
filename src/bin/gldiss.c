#include "../rt.h"
#include "../bytecode/image.h"
#include "../util/io.h"
#include "../logging.h"

extern int gs_argc;
extern const char **gs_argv;

Err *gs_main() {
  if (gs_argc <= 1) {
    fprintf(stderr, "Usage: %s [files ...]\n", gs_argv[0]);
    GS_FAILWITH("Not enough arguments supplied", NULL);
  }

  for (const char **it = gs_argv + 1; it != gs_argv + gs_argc; ++it) {
    fprintf(stderr, "File: %s\n", *it);
    AllocMeta allocMeta;
    u8 *buf;
    GS_TRY(gs_read_file(*it, &allocMeta, &buf));

    Image img;
    GS_TRY(gs_index_image(allocMeta.count, buf, &img));

    gs_stderr_dump(&img);

    gs_free_image(&img);
    gs_free(buf, allocMeta);
  }

  GS_RET_OK;
}
