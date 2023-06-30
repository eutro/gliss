#pragma once

#include "../rt.h"
#include "../gc/gc_type.h"

DEFINE_GC_TYPE(
  Cons,
  GC(FIX, Tagged), Val, car,
  GC(FIX, Tagged), Val, cdr
);
DEFINE_GC_TYPE(
  Box,
  GC(FIX, Tagged), Val, value
);
typedef anyptr anyptrs[1];
DEFINE_GC_TYPE(
  RawArray,
  NOGC(FIX), u32, len,
  GC(RSZ(len), Raw), anyptrs, data
);
DEFINE_GC_TYPE(
  Array,
  NOGC(FIX), u32, len,
  GC(RSZ(len), Tagged), ValArray, data
);
DEFINE_GC_TYPE(
  OpaqueArray,
  NOGC(FIX), u32, len,
  NOGC(RSZ(len)), anyptrs, data
);
DEFINE_GC_TYPE(
  WordArray,
  NOGC(FIX), u32, len,
  NOGC(RSZ(len)), ValArray, data
);

#define SYMBOL_TYPE 0
#define STRING_TYPE 1
#define BYTESTRING_TYPE 2
#define CONS_TYPE 3
#define RAW_ARRAY_TYPE 4
#define ARRAY_TYPE 5
#define OPAQUE_ARRAY_TYPE 6
#define WORD_ARRAY_TYPE 7
#define NATIVE_CLOSURE_TYPE 8
#define INTERP_CLOSURE_TYPE 9
#define SYM_TABLE_TYPE 10
#define SYM_TABLE_BUCKET_TYPE 11
#define IMAGE_TYPE 12
#define BOX_TYPE 13

Err *gs_add_primitive_types(void);
Err *gs_add_primitives(void);
