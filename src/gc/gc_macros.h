#pragma once

// include after gc.h

#include "../util/cast.h"

/** Get the large object from the header pointer */
#define GC_LARGE_OBJECT(ptr) (&PTR_REF(LargeObject, (u8 *)ptr - offsetof(LargeObject, data)))

// OK for reads and writes, aliases as u8
#define GC_PTR_HEADER_REF(ptr) ((u8 *)(ptr) - sizeof(u64))
#define GC_HEADER_MARK(header) (((u8 *)(header))[1])
// OK for reads and writes, aliasing through union
#define GC_HEADER_GEN(header) (PTR_REF(u16, (u16 *)(header) + 1))
#define GC_HEADER_TY(header) (PTR_REF(u32, (u32 *)(header) + 1))

#ifndef __BYTE_ORDER__
#  error "unknown byte order"
#else
#  if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
// s-a ok since we're reading through a union
#    define READ_FORWARDED(headerRef) ((anyptr) (uptr) (PTR_REF(u64, (headerRef)) >> 8))
#    define FORWARDING_HEADER(forwarded) (((u64) HtForwarding << 56) | ((u64) (uptr) (forwarded) >> 8))
#    define GC_BUILD_HEADER(tag, mark, gen, ty)           \
  (((u64) (u8) tag) |                                     \
   ((u64) (u8) mark << 8) |                               \
   ((u64) (u16) gen << 16) |                              \
   ((u64) (u32) ty << 32))
#  else
#    define READ_FORWARDED(headerRef) ((anyptr) (uptr) (PTR_REF(u64, (headerRef)) & 0x00FFFFFFFFFFFFFF))
#    define FORWARDING_HEADER(forwarded) (((u64) HtForwarding << 56) | (u64) (uptr) (forwarded))
#    define GC_BUILD_HEADER(tag, mark, gen, ty)           \
  (((u64) (u8) tag << 56) |                               \
   ((u64) (u8) mark << 48) |                              \
   ((u64) (u16) gen << 32) |                              \
   ((u64) (u32) ty))
#  endif
#endif
