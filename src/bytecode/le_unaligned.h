#pragma once

#include "../rt_prims.h"

static inline u16 read_u16(u8 **ip) {
  u8 lo = *(*ip)++;
  u8 hi = *(*ip)++;
  return ((u16) hi << INSN_BITS) | (u16) lo;
}

static inline u32 read_u32(u8 **ip) {
  u8 ll = *(*ip)++;
  u8 lh = *(*ip)++;
  u8 hl = *(*ip)++;
  u8 hh = *(*ip)++;
  return
    ((u32) hh << (3 * INSN_BITS)) |
    ((u32) hl << (2 * INSN_BITS)) |
    ((u32) lh << INSN_BITS) |
    (u32) ll;
}
