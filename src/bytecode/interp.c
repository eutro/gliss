#include "interp.h"
#include "../rt.h"

#include <string.h> // memmove

#define DYN1OP(NAME, EXPR) static Val dyn1op_##NAME(Val x) { return (EXPR); }
#include "dyn1ops.h"
#undef DYN1OP

#define DYN1OP(NAME, _EXPR) dyn1op_##NAME,
static Dyn1Op dyn1ops[] = {
  #include "dyn1ops.h"
};
#undef DYN1OP

#define DYN2OP(NAME, EXPR) static Val dyn2op_##NAME(Val x, Val y) { return (EXPR); }
#include "dyn2ops.h"
#undef DYN2OP
#define DYN2OP(NAME, _EXPR) dyn2op_##NAME,
static Dyn2Op dyn2ops[] = {
  #include "dyn2ops.h"
};
#undef DYN2OP

SymTable *gs_global_syms;

static Err *gs_interp_closure_call(GS_CLOSURE_ARGS) {
  InsnSeq *insns = &((InterpClosure *) self)->insns;
  return gs_interp(insns, argc, args, retc, rets);
}

InterpClosure gs_interp_closure(InsnSeq insns) {
  return (InterpClosure) {{gs_interp_closure_call}, insns};
}

Err *gs_interp(
  InsnSeq *insns /* must be verified */,
  u16 argc,
  Val *args,
  u16 retc,
  Val *rets
) {
  Val stack[insns->maxStack];
  Val locals[insns->locals];

  Insn *ip = insns->insns;
  Val *sp = stack;
  // no end checking if the insns are verified
  while (true) {
    switch (*ip++) {
    case NOP: break;
    case BR: ip += *(i16 *)ip; break;
    case BR_IF: {
      i16 off = *(i16 *)ip;
      if (*--sp) {
        ip += off;
      } else {
        ip++;
      }
      break;
    }
    case RET: {
      u16 count = *ip;
      sp -= count;
      GS_FAIL_IF(count > retc, "Returning too many values", NULL);
      memmove(rets, sp, count * sizeof(Val));
      return NULL;
    }
    case CONST_1: {
      *sp++ = (Val) *ip++;
      break;
    }
    case CONST_2: {
      u16 hi = *ip++;
      u16 lo = *ip++;
      *sp++ = ((Val) hi << INSN_BITS) | (Val) lo;
      break;
    }
    case CONST_4: {
      u16 hh = *ip++;
      u16 hl = *ip++;
      u16 lh = *ip++;
      u16 ll = *ip++;
      *sp++ =
        ((Val) hh << (3 * INSN_BITS)) |
        ((Val) hl << (2 * INSN_BITS)) |
        ((Val) lh << INSN_BITS) |
        (Val) ll;
      break;
    }
    case DYN_1: {
      Val arg = *--sp;
      *sp++ = dyn1ops[*ip++](arg);
      break;
    }
    case DYN_2: {
      Val rhs = *--sp;
      Val lhs = *--sp;
      *sp++ = dyn2ops[*ip++](lhs, rhs);
      break;
    }
    case CALL: {
      u16 argc = *ip++;
      u16 retc = *ip++;
      Closure *f = VAL2PTR(Closure, *--sp);
      sp -= argc;
      GS_TRY(f->call(f, argc, sp, retc, sp));
      sp += retc;
      break;
    }
    case INTERN: {
      u16 len = *ip++;
      Symbol *sym = gs_intern(
        gs_global_syms,
        (Utf8Str) {
          (u8 *) ip,
          (u32) len
        }
      );
      ip += (len + 1) / 2;
      *sp++ = PTR2VAL(sym);
      break;
    }
    case LOCAL_REF: {
      *sp++ = locals[*ip++];
      break;
    }
    case LOCAL_SET: {
      locals[*ip++] = *--sp;
      break;
    }
    case ARG_REF: {
      u16 arg = *ip++;
      if (arg >= argc) {
        GS_FAILWITH("Argument out of range", NULL);
      }
      *sp++ = args[arg];
      break;
    }
    default: GS_FAILWITH("Unrecognised opcode", NULL);
    }
  }
}
