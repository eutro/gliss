#include "primitives.h"

static Err *intern_fallible(SymTable *table, Symbol **out, Utf8Str str) {
  *out = gs_intern(table, str);
  GS_FAIL_IF(!*out, "Could not intern symbol", NULL);
  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, symbol_set_value) {
  GS_CHECK_ARITY(2, 1);
  Symbol *sym = VAL2PTR(Symbol, args[0]);
  // printf("Now setting: %s (..%u)\n", sym->name.bytes, sym->name.len);
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

Err *gs_add_primitives(SymTable *table) {
  Symbol *sym;

  GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("symbol-set-value!")));
  sym->value = PTR2VAL_NOGC(&symbol_set_value);

  GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("symbol-set-macro!")));
  sym->value = PTR2VAL_NOGC(&symbol_set_macro);

  GS_RET_OK;
}
