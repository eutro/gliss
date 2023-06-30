#include "interp.h"
#include "../rt.h"
#include "ops.h"
#include "le_unaligned.h"
#include "primitives.h"
#include "../gc/gc.h"
#include "../logging.h"

#include "tck.h"

#include <string.h> // memmove

struct ShadowStack gs_shadow_stack;

static Err *gs_interp_closure_call(GS_CLOSURE_ARGS);

Err *gs_interp_closure(Image *img, u32 codeRef, Val *args, u16 argv, InterpClosure **out) {
  InterpClosure *cls;
  GS_TRY(gs_gc_alloc_array(INTERP_CLOSURE_TYPE, argv, (anyptr *)&cls));
  cls->parent.call = gs_interp_closure_call;
  cls->assignedTo = NULL;
  cls->img = img;
  cls->codeRef = codeRef;
  memcpy(cls->captured, args, argv * sizeof(Val));
  *out = cls;
  GS_RET_OK;
}

#include "le_unaligned.h"

static Err *alloc_list(Val *arr, u16 len, Val *out) {
  Val *iter = arr + len - 1;
  Val *end = arr - 1;
  Val ret = VAL_NIL;
  for (; iter != end; iter--) {
    Val *pair;
    GS_TRY(gs_gc_alloc(CONS_TYPE, (anyptr *)&pair));
    pair[0] = *iter;
    pair[1] = ret;
    ret = PTR2VAL_GC(pair);
  }
  *out = ret;
  GS_RET_OK;
}

void gs_stderr_dump_closure(InterpClosure *closure) {
  gs_stderr_dump_code(
    closure->img,
    closure->img->codes->values[closure->codeRef]
  );
}

static Err *gs_interp(
  InterpClosure *self, // must be verified
  u16 argc,
  Val *args,
  u16 retc,
  Val *rets
) {
  GS_FAIL_IF(gs_shadow_stack.depth >= GS_STACK_MAX_DEPTH, "Stack overflow", NULL);

  GS_TRY(gs_bake_image(self->img));
  CodeInfo *insns = self->img->codes->values[self->codeRef];
  Val stack[get32le(insns->maxStack)];
  Val locals[get32le(insns->locals)];

  Insn
    *codeStart = insns->code,
    *ip = codeStart;
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
      }
      break;
    }
    case RET: {
      u8 count = *ip++;
      sp -= count;
      GS_FAIL_IF(count > retc, "Returning too many values", NULL);
      memmove(rets, sp, count * sizeof(Val));
      GS_RET_OK;
    }
    case LDC: {
      u32 idx = read_u32(&ip);
      *sp++ = self->img->constantsBaked->values[idx];
      break;
    }
    case SYM_DEREF: {
      Val symV = *--sp;
      GS_FAIL_IF(!is_type(symV, SYMBOL_TYPE), "Not a symbol", NULL);
      Symbol *sym = VAL2PTR(Symbol, symV);
      LOG_TRACE("Dereferenced symbol: %.*s", sym->name->len, sym->name->bytes);
      *sp++ = sym->value;
      break;
    }
    case LAMBDA: {
      InterpClosure *cls;
      GS_TRY(gs_gc_alloc(INTERP_CLOSURE_TYPE, (anyptr *) &cls));
      u32 idx = read_u32(&ip);
      u16 arity = read_u16(&ip);
      sp -= arity;
      GS_TRY(gs_interp_closure(self->img, idx, sp, arity, &cls));
      *sp++ = PTR2VAL_GC(cls);
      break;
    }
    case CALL: {
      u8 argc = *ip++;
      u8 retc = *ip++;
      sp -= argc;
      Val fv = *--sp;
      if (!is_callable(fv)) {
        GS_FAILWITH("Not a function", NULL);
      }
      Closure *f = VAL2PTR(Closure, fv);

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
    case RESTARG_REF: {
      u8 arg = *ip++;
      if (arg > argc) {
        GS_FAILWITH("Rest argument out of range", NULL);
      }
      u16 size = argc - (u16) arg;
      GS_TRY(alloc_list(args + arg, size, sp));
      sp++;
      break;
    }
    case THIS_REF: {
      *sp++ = PTR2VAL_GC(self);
      break;
    }
    case CLOSURE_REF: {
      u8 arg = *ip++;
      GS_FAIL_IF(arg >= self->capturec, "Captured value out of bounds", NULL);
      *sp++ = self->captured[arg];
      break;
    }
    default: {
      LOG_ERROR("Unrecognised opcode: 0x%" PRIx8, ip[-1]);
      GS_FAILWITH("Unrecognised opcode", NULL);
    }
    }
  }
}

static Err *gs_interp_closure_call(GS_CLOSURE_ARGS) {
  InterpClosure *closureSelf = (InterpClosure *)self;
  gs_shadow_stack.depth++;
  Err *err = gs_interp(closureSelf, argc, args, retc, rets);
  gs_shadow_stack.depth--;
  if (err) {
#define FAIL                                            \
    GS_FAILWITH_FRAME(                                  \
      GS_ERR_FRAME(                                     \
        GS_UTF8_CSTR("lambda body"),                    \
        closureSelf->assignedTo                         \
        ? GS_DECAY_BYTES(closureSelf->assignedTo->name) \
        : GS_UTF8_CSTR("{unknown}"),                    \
        GS_UTF8_CSTR_DYN(GS_FILENAME),                  \
        __LINE__                                        \
      ),                                                \
        err                                             \
    )
    FAIL;
#undef FAIL
  }
  return err;
}
