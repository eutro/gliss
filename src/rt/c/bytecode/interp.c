/**
 * Copyright (C) 2023 eutro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "interp.h"
#include "../rt.h"
#include "ops.h"
#include "le_unaligned.h"
#include "primitives.h"
#include "../gc/gc.h"
#include "../logging.h"

#include "tck.h"

#include <string.h> // memmove

struct ShadowStack gs_shadow_stack = { 0, NULL };

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
      if (!is_type(symV, SYMBOL_TYPE)) {
        GS_FAILWITH_VAL_MSG("Not a symbol", symV);
      }
      Symbol *sym = VAL2PTR(Symbol, symV);
      LOG_TRACE("Dereferenced symbol: %.*s", sym->name->len, sym->name->bytes);
      *sp++ = sym->value;
      break;
    }
    case LAMBDA: {
      InterpClosure *cls;
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
      GS_TRY(gs_alloc_list(args + arg, size, sp));
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

void gs_interp_dump_stack() {
  fprintf(stderr, GREEN "Stack trace" NONE ":\n");
  for (struct StackFrame *frame = gs_shadow_stack.frame; frame; frame = frame->next) {
    fprintf(stderr, "  " BROWN "at" NONE " " CYAN "%.*s" NONE "\n", frame->name.len, frame->name.bytes);
  }
}

static Err *gs_interp_closure_call(GS_CLOSURE_ARGS) {
  InterpClosure *closureSelf = (InterpClosure *)self;
  Utf8Str name = closureSelf->assignedTo
    ? GS_DECAY_BYTES(closureSelf->assignedTo->name)
    : GS_UTF8_CSTR("{unknown}");
  struct StackFrame frame = {
    name,
    gs_shadow_stack.frame
  };
  gs_shadow_stack.frame = &frame;
  gs_shadow_stack.depth++;
  //LOG_DEBUG("Called (%" PRIu32 "): %.*s", gs_shadow_stack.depth, name.len, name.bytes);
  Err *err = gs_interp(closureSelf, argc, args, retc, rets);
  //LOG_DEBUG("Returned (%" PRIu32 "): %.*s", gs_shadow_stack.depth, name.len, name.bytes);
  gs_shadow_stack.depth--;
  gs_shadow_stack.frame = frame.next;
  if (err) {
#define FAIL                                            \
    GS_FAILWITH_FRAME(                                  \
      GS_ERR_FRAME(                                     \
        GS_UTF8_CSTR("lambda body"),                    \
        name,                                           \
        GS_UTF8_CSTR_DYN(GS_FILENAME),                  \
        __LINE__                                        \
      ),                                                \
      PTR2VAL_NOGC(NULL),                               \
      err                                               \
    )
    FAIL;
#undef FAIL
  }
  return err;
}
