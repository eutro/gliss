#include <string.h>

#include "primitives.h"
#include "../gc/gc.h"
#include "../logging.h"
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

static Field CONS_FIELDS[] = {
  {
    .offset = 0,
    .size = sizeof(Val),
  },
  {
    .offset = sizeof(Val),
    .size = sizeof(Val),
  }
};
static TypeInfo CONS_TYPEINFO = {
  .layout = {
    .align = alignof(Val),
    .size = 2 * sizeof(Val),
    .gcFieldc = 2,
    .isArray = false,
    .fieldc = 2,
    .fields = CONS_FIELDS,
  },
  .protos = {NULL, NULL, 0},
  .name = GS_UTF8_CSTR("cons")
};

static Field INTERP_CLOSURE_FIELDS[] = {
  {
    .offset = offsetof(InterpClosure, img),
    .size = sizeof(Image),
  },
  {
    .offset = offsetof(InterpClosure, codeRef),
    .size = sizeof(u32),
  }
};
static TypeInfo INTERP_CLOSURE_TYPEINFO = {
  .layout = {
    .align = alignof(InterpClosure),
    .size = sizeof(InterpClosure),
    .gcFieldc = 0,
    .isArray = false,
    .fieldc = 2,
    .fields = INTERP_CLOSURE_FIELDS,
  },
  .protos = {NULL, NULL, 0},
  .name = GS_UTF8_CSTR("closure")
};

Err *gs_add_primitives(SymTable *table) {
  Symbol *sym;

  GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("symbol-set-value!")));
  sym->value = PTR2VAL_NOGC(&symbol_set_value);

  GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("symbol-set-macro!")));
  sym->value = PTR2VAL_NOGC(&symbol_set_macro);

  if (gs_global_gc) {
    TypeIdx consTy;
    GS_TRY(gs_gc_push_type(CONS_TYPEINFO, &consTy));
    GS_FAIL_IF(consTy != CONS_TYPE, "Wrong type index", NULL);

    GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("cons")));
    sym->value = PTR2VAL_NOGC(&cons);

    GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("car")));
    sym->value = PTR2VAL_NOGC(&car);

    GS_TRY(intern_fallible(table, &sym, GS_UTF8_CSTR("cdr")));
    sym->value = PTR2VAL_NOGC(&cdr);

    TypeIdx interpClosureTy;
    GS_TRY(gs_gc_push_type(INTERP_CLOSURE_TYPEINFO, &interpClosureTy));
    GS_FAIL_IF(interpClosureTy != INTERP_CLOSURE_TYPE, "Wrong type index", NULL);
  }

  GS_RET_OK;
}
