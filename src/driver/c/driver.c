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

#include "bytecode/image.h"
#include "bytecode/interp.h"
#include "bytecode/primitives.h"
#include "driver.h"
#include "gc/gc.h"
#include "logging.h"
#include "rt.h"

Err *gs_run_raw_image(u32 size, const u8 *buf) {
  Image *img;
  GS_TRY(gs_index_image(size, buf, &img));
  GS_TRY(gs_run_image(img));
  GS_RET_OK;
}

Err *gs_run_image(Image *img) {
  GS_TRY(gs_alloc_sym_table());

  PUSH_RAW_GC_ROOTS(2, top);
  roots_top.arr[0].len = 1;
  roots_top.arr[0].arr = (anyptr *)&gs_global_syms;

  GS_TRY(gs_add_primitives());

  if (img->start.code) {
    InterpClosure *start;
    GS_TRY(gs_interp_closure(img, img->start.code - 1, NULL, 0, &start));

    LOG_INFO("%s", "Found start function, running");

    GS_TRY(gs_gc_push_scope());
    PUSH_DIRECT_GC_ROOTS(0, start, NULL);
    GS_TRY_C(gs_call(&start->parent, 0, NULL, 0, NULL), POP_GC_ROOTS(top));
    POP_GC_ROOTS(start);
    GS_TRY_C(gs_gc_pop_scope(), POP_GC_ROOTS(top));
  } else {
    LOG_INFO("%s", "No start function defined");
  }

  Symbol *main;
  GS_TRY(gs_intern(GS_UTF8_CSTR("main"), &main));
  if (main->value == PTR2VAL_GC(main)) {
    LOG_INFO("%s", "No main symbol defined");
  } else {
    LOG_INFO("%s", "Found main symbol, calling");
    GS_TRY(gs_gc_push_scope());
    PUSH_DIRECT_GC_ROOTS(0, main, NULL);
    Val ret;
    GS_TRY_C(gs_call(&main->fn, 0, NULL, 1, &ret), POP_GC_ROOTS(top));
    POP_GC_ROOTS(main);
    GS_TRY_C(gs_gc_pop_scope(), POP_GC_ROOTS(top));
  }

  LOG_INFO("%s", "Done");

  POP_GC_ROOTS(top);
  GS_RET_OK;
}
