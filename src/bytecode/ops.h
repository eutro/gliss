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

  DROP = 0x01,

  RET = 0x02,

  BR = 0x03,
  BR_IF_NOT = 0x04,

  CONST_1 = 0x05,
  CONST_2 = 0x06,
  CONST_4 = 0x07,
  CONST_8 = 0x08,
  LDC = 0x09,

  DYN_1 = 0x0A,
  DYN_2 = 0x0B,

  LAMBDA = 0x0C,

  CALL = 0x10,

  INTERN = 0x11,

  LOCAL_REF = 0x12,
  LOCAL_SET = 0x13,
  ARG_REF = 0x14,
  RESTARG_REF = 0x15,
  THIS_REF = 0x16,
  CLOSURE_REF = 0x17,
} Opc;

typedef Val (*Dyn1Op)(Val);
typedef Val (*Dyn2Op)(Val, Val);
