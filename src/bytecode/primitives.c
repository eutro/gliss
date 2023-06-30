#include <string.h>

#define DO_DECLARE_GC_METADATA 1

#include "primitives_impl.h"

#define IMPL(NAME, C_NAME, ...)                 \
  GS_TOP_CLOSURE(STATIC, C_NAME) __VA_ARGS__
#include "primitives_impl.c"
#undef IMPL

Err *gs_add_primitives() {
  GS_FAIL_IF(!gs_global_gc, "No garbage collector", NULL);

  Symbol *sym;
  NativeClosure *cls;

#define ADD0(SYM, COMPUTE, VAL)                                         \
  do {                                                                  \
    GS_TRY(gs_intern(GS_UTF8_CSTR(SYM), &sym));                         \
    COMPUTE;                                                            \
    sym->value = (VAL);                                                 \
  } while(0)
#define ALLOC_CLS(CLS)                                                  \
  do {                                                                  \
    GS_TRY(gs_gc_alloc(NATIVE_CLOSURE_TYPE, (anyptr *)&cls));           \
    cls->parent = (CLS);                                                \
  } while(0)
#define ADD(SYM, CLS) ADD0(SYM, ALLOC_CLS(CLS), PTR2VAL_GC(cls))

#define IMPL(NAME, C_NAME, ...) ADD(NAME, C_NAME)
#include "primitives_impl.c"
#undef IMPL

  ADD0("eof",, VAL_EOF);

#undef ADD

  GS_RET_OK;
}

Err *gs_add_primitive_types() {
  GS_FAIL_IF(!gs_global_gc, "No garbage collector", NULL);

  TypeIdx idx;

#define ADD(Type, CAPS)                                     \
  do {                                                      \
    GS_TRY(gs_gc_push_type(Type##_INFO, &idx));             \
    GS_FAIL_IF(idx != CAPS##_TYPE, "Wrong type index", NULL);   \
  } while(0)

  ADD(Symbol, SYMBOL);
  ADD(InlineUtf8Str, STRING);
  ADD(InlineBytes, BYTESTRING);
  ADD(Cons, CONS);
  ADD(RawArray, RAW_ARRAY);
  ADD(Array, ARRAY);
  ADD(OpaqueArray, OPAQUE_ARRAY);
  ADD(WordArray, WORD_ARRAY);
  ADD(NativeClosure, NATIVE_CLOSURE);
  ADD(InterpClosure, INTERP_CLOSURE);
  ADD(SymTable, SYM_TABLE);
  ADD(SymTableBucket, SYM_TABLE_BUCKET);
  ADD(Image, IMAGE);
  ADD(Box, BOX);

#undef ADD

  GS_RET_OK;
}
