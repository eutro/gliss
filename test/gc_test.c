#include "gc/gc.h"
#include "gc/gc_macros.h"

typedef Val ConsCell[2];

Err *gs_main() {
  {
    u8 exampleHeader[sizeof(u64) * 3];

    anyptr testObj = exampleHeader + sizeof(u64);
    u8 *testHeader = GC_PTR_HEADER_REF(testObj);
    GS_FAIL_IF(exampleHeader != testHeader, "Wrong header", NULL);
    GS_FAIL_IF(1 != (u8 *) &GC_HEADER_MARK(testHeader) - exampleHeader, "Wrong mark", NULL);
    GS_FAIL_IF(2 != (u8 *) &GC_HEADER_GEN(testHeader) - exampleHeader, "Wrong gen", NULL);
    GS_FAIL_IF(4 != (u8 *) &GC_HEADER_TY(testHeader) - exampleHeader, "Wrong type", NULL);
  }

  GcAllocator gc;
  GS_TRY(gs_gc_init(GC_DEFAULT_CONFIG, &gc));

  Field fields[] = {
    {
      .offset = 0,
      .size = sizeof(Val),
    },
    {
      .offset = sizeof(Val),
      .size = sizeof(Val),
    },
  };
  TypeIdx consIdx;
  gs_gc_push_type(
    (TypeInfo) {
      .layout = {
        .align = alignof(Val),
        .size = sizeof(Val) * 2,
        .gcFieldc = 2,
        .isArray = false,
        .fieldc = 2,
        .fields = fields,
      },
      .protos = {
        .keys = NULL,
        .tables = NULL,
        .size = 0
      },
      .name = GS_UTF8_CSTR("cons")
    },
    &consIdx
  );

  GS_TRY(gs_gc_push_scope());

  anyptr pair1, pair2, pair3;
  GS_TRY(gs_gc_alloc(consIdx, &pair1));
  PTR_REF(ConsCell, pair1)[0] = FIX2VAL(3);
  PTR_REF(ConsCell, pair1)[1] = VAL_NIL;
  GS_TRY(gs_gc_alloc(consIdx, &pair2));
  PTR_REF(ConsCell, pair2)[0] = FIX2VAL(2);
  PTR_REF(ConsCell, pair2)[1] = PTR2VAL_GC(pair1);
  GS_TRY(gs_gc_alloc(consIdx, &pair3));
  PTR_REF(ConsCell, pair3)[0] = FIX2VAL(1);
  PTR_REF(ConsCell, pair3)[1] = PTR2VAL_GC(pair2);

  anyptr pair4;
  GS_TRY(gs_gc_alloc(consIdx, &pair4));
  PTR_REF(ConsCell, pair4)[0] = FIX2VAL(0);
  PTR_REF(ConsCell, pair4)[1] = PTR2VAL_GC(pair3);

  Val toPreserve = PTR2VAL_GC(pair3);
  PUSH_DIRECT_GC_ROOTS(1, &toPreserve);

  GS_FAIL_IF_C(
    pair1 == pair2 ||
    pair2 == pair3 ||
    pair3 == pair1,
    "Aliasing allocations",
    NULL,
    POP_GC_ROOTS()
  );

  GS_TRY(gs_gc_pop_scope());
  POP_GC_ROOTS();

  GS_TRY(gs_gc_dispose(&gc));

  GS_RET_OK;
}
