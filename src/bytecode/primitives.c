#include <string.h>

#define DO_DECLARE_GC_METADATA 1
#include "primitives.h"
#include "../gc/gc.h"
#include "../logging.h"
#include "interp.h"

GS_TOP_CLOSURE(STATIC, symbol_set_value) {
  GS_CHECK_ARITY(2, 1);
  Symbol *sym = VAL2PTR(Symbol, args[0]);
  LOG_TRACE("Setting symbol: %.*s", sym->name->len, sym->name->bytes);
  Val toWrite = args[1];
  if (VAL_IS_GC_PTR(toWrite)) {
    gs_gc_write_barrier(&sym->value, VAL2PTR(u8, toWrite));
  }
  sym->value = toWrite;
  rets[0] = args[0];
  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, symbol_set_macro) {
  GS_CHECK_ARITY(2, 1);
  Symbol *sym = VAL2PTR(Symbol, args[0]);
  sym->isMacro = VAL_TRUTHY(args[1]);
  rets[0] = args[0];
  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, cons) {
  GS_CHECK_ARITY(2, 1);
  anyptr pair;
  GS_TRY(gs_gc_alloc(CONS_TYPE, &pair));
  memcpy(pair, args, 2 * sizeof(Val));
  rets[0] = PTR2VAL_GC(pair);
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, car) {
  GS_CHECK_ARITY(1, 1);
  u8 *raw = VAL2PTR(u8, args[0]);
  GS_FAIL_IF(gs_gc_typeinfo(raw) != CONS_TYPE, "Not a pair", NULL);
  memcpy(rets, (Val *)raw, sizeof(Val));
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, cdr) {
  GS_CHECK_ARITY(1, 1);
  u8 *raw = VAL2PTR(u8, args[0]);
  GS_FAIL_IF(gs_gc_typeinfo(raw) != CONS_TYPE, "Not a pair", NULL);
  memcpy(rets, (Val *)raw + 1, sizeof(Val));
  GS_RET_OK;
}

DEFINE_GC_TYPE(
  Cons,
  GC(FIX, Tagged), Val, car,
  GC(FIX, Tagged), Val, cdr
);

Err *gs_add_primitive_types() {
  GS_FAIL_IF(!gs_global_gc, "No garbage collector", NULL);

  TypeIdx idx;

  GS_TRY(gs_gc_push_type(Symbol_INFO, &idx));
  GS_FAIL_IF(idx != SYMBOL_TYPE, "Wrong type index", NULL);

  GS_TRY(gs_gc_push_type(InlineUtf8Str_INFO, &idx));
  GS_FAIL_IF(idx != STRING_TYPE, "Wrong type index", NULL);

  GS_TRY(gs_gc_push_type(InlineBytes_INFO, &idx));
  GS_FAIL_IF(idx != BYTESTRING_TYPE, "Wrong type index", NULL);

  GS_TRY(gs_gc_push_type(Cons_INFO, &idx));
  GS_FAIL_IF(idx != CONS_TYPE, "Wrong type index", NULL);

  GS_TRY(gs_gc_push_type(NativeClosure_INFO, &idx));
  GS_FAIL_IF(idx != NATIVE_CLOSURE_TYPE, "Wrong type index", NULL);

  GS_TRY(gs_gc_push_type(InterpClosure_INFO, &idx));
  GS_FAIL_IF(idx != INTERP_CLOSURE_TYPE, "Wrong type index", NULL);

  GS_RET_OK;
}

Err *gs_add_primitives(SymTable *table) {
  GS_FAIL_IF(!gs_global_gc, "No garbage collector", NULL);

  Symbol *sym;
  NativeClosure *cls;

#define ADD(SYM, CLS)                                                   \
  do {                                                                  \
    GS_TRY(gs_intern(table, GS_UTF8_CSTR(SYM), &sym));                  \
    GS_TRY(gs_gc_alloc(NATIVE_CLOSURE_TYPE, (anyptr *)&cls));           \
    cls->parent = (CLS);                                                \
    sym->value = PTR2VAL_GC(cls);                                       \
  } while(0)

  ADD("symbol-set-value!", symbol_set_value);
  ADD("symbol-set-macro!", symbol_set_macro);
  ADD("cons", cons);
  ADD("car", car);
  ADD("cdr", cdr);

  GS_RET_OK;
}
