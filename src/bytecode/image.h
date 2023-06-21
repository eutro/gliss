#pragma once

#include "../rt.h"

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
  CNumber,
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
// a number, only the bottom 63 bits can be used, interpreted as
// signed at runtime
struct ConstNumber {
  ConstInfo hd;
  u64le value;
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

// an interpretable block of code
typedef struct CodeInfo {
  u32le len;
  u32le locals;
  u32le maxStack;
  u8 code[1]; // padded to multiple of 4
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

typedef struct Image {
  u8 *buf;
  // u32 magic: "gls\0"
  u32 version; // 1
  // constant table
  struct {
    u32 len;
    ConstInfo **values;
  } constants;
  // code table
  struct {
    u32 len;
    CodeInfo **values;
  } codes;
  // binding assoc list
  struct {
    u32 len;
    BindingInfo *pairs;
  } bindings;
  // start code block, called to run
  struct {
    // C-side: 0 if absent, 1-indexed,
    // but 0-indexed in the bytecode
    u32 code;
  } start;
} Image;

Err *indexImage(u32 len, u8 *buf, Image *ret);
