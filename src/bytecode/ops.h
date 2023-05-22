#pragma once

#include "../rt_prims.h"

typedef u8 Insn;

#define INSN_BITS 8

typedef struct InsnSeq {
  Insn *insns;
  u32 len;
  u32 locals;
  u32 maxStack;
} InsnSeq;

typedef enum Opc {
  NOP = 0x00,

  RET = 0x01,

  BR = 0x02,
  BR_IF = 0x03,

  CONST_1 = 0x04,
  CONST_2 = 0x05,
  CONST_4 = 0x06,
  CONST_8 = 0x07,

  DYN_1 = 0x08,
  DYN_2 = 0x09,

  CALL = 0x10,

  INTERN = 0x11,

  LOCAL_REF = 0x12,
  LOCAL_SET = 0x13,
  ARG_REF = 0x14,
} Opc;

typedef Val (*Dyn1Op)(Val);
typedef Val (*Dyn2Op)(Val, Val);
