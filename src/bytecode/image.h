#pragma once

#include "../rt.h"
#include "../gc/gc_type.h"

// integers are encoded in little-endian byte order
typedef struct u32le { u32 raw; } u32le;
typedef struct u64le { u64 raw; } u64le;

u32 get32le(u32le le);
u64 get64le(u64le le);

// a reference to a constant, from the constant table
typedef u32le ConstRef;

// a reference to a block of code, from the code table
typedef u32le CodeRef;

// types of constants
enum ConstTy {
  CLambda,
  CList,
  CDirect,
  CSymbol,
  CString,
};

// a constant value, as verbatim in the bytecode
typedef struct ConstInfo {
  u32le /* ConstTy */ ty;
} ConstInfo;

// a symbol or a string
struct ConstBytevec {
  ConstInfo hd;
  u32le len;
  u8 data[1]; // padded to multiple of 4
};
// a direct constant, which fits in a single machine word; like
// fixnums or global constants (nil, eof, booleans)
struct ConstDirect {
  ConstInfo hd;
  u32le lo, hi;
};
// a list of elements; can only refer to elements before it, to avoid cycles
struct ConstList {
  ConstInfo hd;
  u32le len;
  ConstRef elements[1];
};
// a lambda capturing a list of values, still no cycles
struct ConstLambda {
  ConstInfo hd;
  CodeRef code;
  struct {
    u32le len;
    ConstRef elements[1];
  } captured;
};

typedef u8 Insn;

#define INSN_BITS 8

// an interpretable block of code
typedef struct CodeInfo {
  u32le len;
  u32le maxStack;
  u32le locals;
  Insn code[1]; // padded to multiple of 4
} CodeInfo;

// an association of a symbol with a binding
typedef struct BindingInfo {
  ConstRef symbol;
  ConstRef binding;
} BindingInfo;

enum Sections {
  SecConstants = 1,
  SecCodes = 2,
  SecBindings = 3,
  SecStart = 4,
};

DEFINE_GC_TYPE(
  Image,

  GC(FIX, Raw), InlineBytes *, buf,
  // TODO reconsider all the pointers below in case the buffer moves

  // u32 magic: "gls\0" = 0x00736c67 = 7564391
  NOGC(FIX), u32, version, // 1

  // constant table
  GC(FIX, Raw), struct {
    u32 len;
    ConstInfo *values[1];
  } /* OpaqueArray */ *, constants,
  GC(FIX, Raw), struct {
    u32 len;
    Val values[1];
  } /* Array */ *, constantsBaked,

  // code table
  GC(FIX, Raw), struct {
    u32 len;
    CodeInfo *values[1];
  } /* OpaqueArray */ *, codes,

  // binding assoc list
  NOGC(FIX), struct {
    u32 len;
    BindingInfo *pairs;
  }, bindings,

  // start code block, called to run
  NOGC(FIX), struct {
    // C-side: 0 if absent, 1-indexed,
    // but 0-indexed in the bytecode
    u32 code;
  }, start
);

Err *gs_index_image(u32 len, const u8 *buf, Image **ret);
Err *gs_bake_image(Image *img);

void gs_stderr_dump_code(Image *img, CodeInfo *ci);
void gs_stderr_dump(Image *img);
