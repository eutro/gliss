#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
  namespace gliss {
#endif

typedef uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;
typedef uintptr_t uptr;

#ifdef __cplusplus
typedef nullptr_t anyptr;
#else
typedef void *anyptr;
#endif

typedef u64 Val;
typedef i64 ifix;
typedef u64 ufix;
// Layout:
//      [0..62][t2 t1]      (Big Endian)
//
// at least 4 bytes of alignment are expected for pointers
//
//    t2 t1: 00    - fixnum
//    t2 t1: 01    - pointer

#ifdef __cplusplus
  }
}
#endif
