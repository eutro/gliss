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

#include "gc/gc.h"
#include "gc/gc_macros.h"

#undef DO_DECLARE_GC_METADATA
#define DO_DECLARE_GC_METADATA 1
DEFINE_GC_TYPE(
  Cons,
  GC(FIX, Tagged), Val, car,
  GC(FIX, Tagged), Val, cdr
);

DEFINE_GC_TYPE(
  Array,
  NOGC(FIX), u32, len,
  GC(RSZ(len), Tagged), ValArray, vals
);

Err *gs_main() {
  {
    u8 exampleHeader[sizeof(u64) * 3];

    anyptr testObj = exampleHeader + sizeof(u64);
    u8 *testHeader = GC_PTR_HEADER_REF(testObj);
    GS_FAIL_IF(exampleHeader != testHeader, "Wrong header offset", NULL);
    GS_FAIL_IF(1 != (u8 *) &GC_HEADER_MARK(testHeader) - exampleHeader, "Wrong mark offset", NULL);
    GS_FAIL_IF(2 != (u8 *) &GC_HEADER_GEN(testHeader) - exampleHeader, "Wrong gen offset", NULL);
    GS_FAIL_IF(4 != (u8 *) &GC_HEADER_TY(testHeader) - exampleHeader, "Wrong type offset", NULL);

    PTR_REF(u64, testHeader) = GC_BUILD_HEADER(5, 10, 15, 20);
    GS_FAIL_IF(*exampleHeader != 5, "Wrong header tag", NULL);
    GS_FAIL_IF(GC_HEADER_MARK(testHeader) != 10, "Wrong mark value", NULL);
    GS_FAIL_IF(GC_HEADER_GEN(testHeader) != 15, "Wrong gen value", NULL);
    GS_FAIL_IF(GC_HEADER_TY(testHeader) != 20, "Wrong type value", NULL);

    PTR_REF(u64, testHeader) = FORWARDING_HEADER(12345678);
    GS_FAIL_IF(*exampleHeader != HtForwarding, "Wrong header tag", NULL);
    GS_FAIL_IF(READ_FORWARDED(testHeader) != (anyptr) 12345678, "Wrong forwarding pointer", NULL);
  }

  TypeIdx consIdx, arrayIdx;
  GS_TRY(gs_gc_push_type(Cons_INFO, &consIdx));
  GS_TRY(gs_gc_push_type(Array_INFO, &arrayIdx));

  GS_TRY(gs_gc_push_scope());

  anyptr pair1, pair2, pair3;
  GS_TRY(gs_gc_alloc(consIdx, &pair1));
  PTR_REF(Cons, pair1).car = FIX2VAL(3);
  PTR_REF(Cons, pair1).cdr = VAL_NIL;
  GS_TRY(gs_gc_alloc(consIdx, &pair2));
  PTR_REF(Cons, pair2).car = FIX2VAL(2);
  PTR_REF(Cons, pair2).cdr = PTR2VAL_GC(pair1);
  GS_TRY(gs_gc_alloc(consIdx, &pair3));
  PTR_REF(Cons, pair3).car = FIX2VAL(1);
  PTR_REF(Cons, pair3).cdr = PTR2VAL_GC(pair2);

  anyptr pair4;
  GS_TRY(gs_gc_alloc(consIdx, &pair4));
  PTR_REF(Cons, pair4).car = FIX2VAL(0);
  PTR_REF(Cons, pair4).cdr = PTR2VAL_GC(pair3);

  anyptr array, pair5;
  GS_TRY(gs_gc_alloc(consIdx, &pair5));
  PTR_REF(Cons, pair5).car = FIX2VAL(11);
  PTR_REF(Cons, pair5).cdr = VAL_NIL;
  GS_TRY(gs_gc_alloc_array(arrayIdx, 2, &array));
  PTR_REF(Array, array).vals[0] = FIX2VAL(10);
  PTR_REF(Array, array).vals[1] = PTR2VAL_GC(pair5);

  Val toPreserve[] = {
    PTR2VAL_GC(pair3),
    PTR2VAL_GC(array),
  };
  PUSH_DIRECT_GC_ROOTS(2, top, toPreserve);

  GS_FAIL_IF_C(
    pair1 == pair2 ||
    pair2 == pair3 ||
    pair3 == pair1,
    "Aliasing allocations",
    NULL,
    POP_GC_ROOTS(top)
  );

  GS_TRY(gs_gc_pop_scope());
  POP_GC_ROOTS(top);

  GS_RET_OK;
}
