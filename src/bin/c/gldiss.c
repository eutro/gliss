/**
 * Copyright (C) 2023 eutro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "rt.h"
#include "bytecode/image.h"
#include "util/io.h"
#include "logging.h"

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

    Image *img;
    GS_TRY(gs_index_image(allocMeta.count, buf, &img));
    gs_free(buf, allocMeta);

    gs_stderr_dump(img);
  }

  GS_RET_OK;
}
