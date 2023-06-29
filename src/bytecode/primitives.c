#include <string.h>

#include "primitives.h"
#include "../gc/gc.h"
#include "../logging.h"
#undef DO_DECLARE_GC_METADATA
#define DO_DECLARE_GC_METADATA 1
#include "interp.h"

static Err *intern_fallible(SymTable *table, Symbol **out, Utf8Str str) {
  *out = gs_intern(table, str);
  GS_FAIL_IF(!*out, "Could not intern symbol", NULL);
  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, symbol_set_value) {
  GS_CHECK_ARITY(2, 1);
  Symbol *sym = VAL2PTR(Symbol, args[0]);
  LOG_TRACE("Setting symbol: %s", sym->name.bytes);
  sym->value = args[1];
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
  GC(FIX), Val, car,
  GC(FIX), Val, cdr
);

Err *gs_add_primitives(SymTable *table) {
  Symbol *sym;

  GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("symbol-set-value!")));
  sym->value = PTR2VAL_NOGC(&symbol_set_value);

  GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("symbol-set-macro!")));
  sym->value = PTR2VAL_NOGC(&symbol_set_macro);

  if (gs_global_gc) {
    TypeIdx consTy;
    GS_TRY(gs_gc_push_type(Cons_INFO, &consTy));
    GS_FAIL_IF(consTy != CONS_TYPE, "Wrong type index", NULL);

    GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("cons")));
    sym->value = PTR2VAL_NOGC(&cons);

    GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("car")));
    sym->value = PTR2VAL_NOGC(&car);

    GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("cdr")));
    sym->value = PTR2VAL_NOGC(&cdr);

    TypeIdx interpClosureTy;
    GS_TRY(gs_gc_push_type(InterpClosure_INFO, &interpClosureTy));
    GS_FAIL_IF(interpClosureTy != INTERP_CLOSURE_TYPE, "Wrong type index", NULL);
  }

  GS_RET_OK;
}
