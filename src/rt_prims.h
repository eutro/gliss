#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
  namespace gliss {
#endif

// This is for strict aliasing; standards specify that any
// /character-type/ pointer can alias any other type (but not
// vice-versa); however it is possible for (u)int8_t to not be a
// character type, and thus for this not to work (though GCC and Clang
// are cool). This here is therefore a pitiful attempt by me to avoid
// undefined behaviour.
typedef unsigned char u8;
typedef signed char i8;

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
//    t2 t1: 01    - GC pointer
//    t2 t2: 11    - non-GC pointer
//    t2 t1: 10    - constant (any x with (x & 7) == 6 is falsy)

#ifdef __cplusplus
  }
}
#endif
