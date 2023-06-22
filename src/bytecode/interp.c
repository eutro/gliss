#include "interp.h"
#include "../rt.h"
#include "ops.h"
#include "le_unaligned.h"

#include <string.h> // memmove

SymTable *gs_global_syms;
struct ShadowStack gs_shadow_stack;

static Err *gs_interp_closure_call(GS_CLOSURE_ARGS);

InterpClosure gs_interp_closure(Image *img, u32 codeRef) {
  return (InterpClosure) {
    {gs_interp_closure_call},
    img,
    codeRef,
  };
}

#include "le_unaligned.h"

static Err *gs_interp(
  InterpClosure *self, // must be verified
  u16 argc,
  Val *args,
  u16 retc,
  Val *rets
) {
  GS_FAIL_IF(gs_shadow_stack.depth >= GS_STACK_MAX_DEPTH, "Stack overflow", NULL);

  GS_TRY(gs_bake_image(self->img));
  CodeInfo *insns = self->img->codes.values[self->codeRef];
  Val stack[get32le(insns->maxStack)];
  Val locals[get32le(insns->locals)];

  Insn *ip = insns->code;
  Val *sp = stack;
  // no end checking if the insns are verified
  while (true) {
    switch (*ip++) {
    case NOP: break;
    case DROP: {
      --sp;
      break;
    }
    case BR: {
      ip += (i32) read_u32(&ip);
      break;
    }
    case BR_IF_NOT: {
      i32 off = (i32) read_u32(&ip);
      if (VAL_FALSY(*--sp)) {
        ip += off;
      } else {
        ip++;
      }
      break;
    }
    case RET: {
      u8 count = *ip;
      sp -= count;
      GS_FAIL_IF(count > retc, "Returning too many values", NULL);
      memmove(rets, sp, count * sizeof(Val));
      return NULL;
    }
    case LDC: {
      u32 idx = read_u32(&ip);
      *sp++ = self->img->constants.baked[idx];
      break;
    }
    case SYM_DEREF: {
      Symbol *sym = VAL2PTR(Symbol, *--sp);
      *sp++ = sym->value;
      break;
    }
    case LAMBDA: {
      InterpClosure *cls = gs_alloc(GS_ALLOC_META(InterpClosure, 1));
      u32 idx = read_u32(&ip);
      u16 arity = read_u16(&ip);
      *cls = gs_interp_closure(self->img, idx);
      // TODO save closure values
      sp -= arity;
      *sp++ = PTR2VAL(cls);
      break;
    }
    case CALL: {
      u8 argc = *ip++;
      u8 retc = *ip++;
      sp -= argc;
      Closure *f = VAL2PTR(Closure, *--sp);

      Err *err = f->call(f, argc, sp + 1, retc, sp);
      if (err) {
        return err;
      }

      sp += retc;
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
      u8 arg = *ip++;
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

static Err *gs_interp_closure_call(GS_CLOSURE_ARGS) {
  StackFrame frame = {
    gs_shadow_stack.top,
    FKInterp,
  };
  gs_shadow_stack.top = &frame;
  gs_shadow_stack.depth++;
  Err *err = gs_interp((InterpClosure *) self, argc, args, retc, rets);
  gs_shadow_stack.depth--;
  gs_shadow_stack.top = frame.next;
  return err;
}
