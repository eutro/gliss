#pragma once

#include "../rt_prims.h"

typedef enum Opc {
  NOP = 0x00,
  DROP = 0x01,
  RET = 0x02,

  BR = 0x03,
  BR_IF_NOT = 0x04,

  LDC = 0x05,
  SYM_DEREF = 0x06,
  LAMBDA = 0x07,
  CALL = 0x08,

  LOCAL_REF = 0x12,
  LOCAL_SET = 0x13,
  ARG_REF = 0x14,
  RESTARG_REF = 0x15,
  THIS_REF = 0x16,
  CLOSURE_REF = 0x17,
} Opc;
