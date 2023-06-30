#include <string.h>

#define DO_DECLARE_GC_METADATA 1
#include "primitives.h"
#include "../gc/gc.h"
#include "../gc/gc_macros.h"
#include "../logging.h"
#include "interp.h"

DEFINE_GC_TYPE(
  Cons,
  GC(FIX, Tagged), Val, car,
  GC(FIX, Tagged), Val, cdr
);

static bool is_type(Val val, TypeIdx idx) {
  return VAL_IS_GC_PTR(val) && gs_gc_typeinfo(VAL2PTR(u8, val)) == idx;
}

static bool is_list0(Val val) {
  return val == VAL_NIL || is_type(val, CONS_TYPE);
}
static bool is_symbol0(Val val) {
  return is_type(val, SYMBOL_TYPE);
}

GS_TOP_CLOSURE(STATIC, symbol_set_value) {
  GS_CHECK_ARITY(2, 1);
  GS_FAIL_IF(!is_symbol0(args[0]), "Not a symbol", NULL);
  Symbol *sym = VAL2PTR(Symbol, args[0]);
  LOG_TRACE("Setting symbol: %.*s", sym->name->len, sym->name->bytes);
  Val toWrite = args[1];
  if (VAL_IS_GC_PTR(toWrite)) {
    gs_gc_write_barrier(sym, &sym->value, VAL2PTR(u8, toWrite), FieldGcTagged);
  }
  sym->value = toWrite;
  rets[0] = args[0];
  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, symbol_set_macro) {
  GS_CHECK_ARITY(2, 1);
  GS_FAIL_IF(!is_symbol0(args[0]), "Not a symbol", NULL);
  Symbol *sym = VAL2PTR(Symbol, args[0]);
  sym->isMacro = VAL_TRUTHY(args[1]);
  rets[0] = args[0];
  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, cons) {
  GS_CHECK_ARITY(2, 1);
  anyptr pair;
  GS_FAIL_IF(!is_list0(args[1]), "Attempted to create improper list", NULL);
  GS_TRY(gs_gc_alloc(CONS_TYPE, &pair));
  memcpy(pair, args, 2 * sizeof(Val));
  rets[0] = PTR2VAL_GC(pair);
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, car) {
  GS_CHECK_ARITY(1, 1);
  GS_FAIL_IF(!is_type(args[0], CONS_TYPE), "Not a pair", NULL);
  u8 *raw = VAL2PTR(u8, args[0]);
  memcpy(rets, (Val *)raw, sizeof(Val));
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, cdr) {
  GS_CHECK_ARITY(1, 1);
  u8 *raw = VAL2PTR(u8, args[0]);
  GS_FAIL_IF(!is_type(args[0], CONS_TYPE), "Not a pair", NULL);
  memcpy(rets, (Val *)raw + 1, sizeof(Val));
  GS_RET_OK;
}

extern int gs_argc;
extern const char **gs_argv;
GS_TOP_CLOSURE(STATIC, program_args) {
  GS_CHECK_ARITY(0, 1);
  const char **end = gs_argv;
  const char **it = end + gs_argc - 1;
  Val ret = VAL_NIL;
  for (; it != end; --it) {
    u32 len = strlen(*it);
    anyptr argStr;
    GS_TRY(gs_gc_alloc_array(STRING_TYPE, len, &argStr));
    Val args[] = { PTR2VAL_GC(argStr), ret };
    GS_TRY(gs_call(&cons, 2, args, 1, &ret));
  }
  rets[0] = ret;
  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, is_eq) {
  GS_CHECK_ARITY(2, 1);
  rets[0] = BOOL2VAL(args[0] == args[1]);
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, is_list) {
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(is_list0(args[0]));
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, is_string) {
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(is_type(args[0], STRING_TYPE));
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, is_bytestring) {
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(is_type(args[0], BYTESTRING_TYPE));
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, is_symbol) {
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(is_symbol0(args[0]));
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, is_number) {
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(VAL_IS_FIXNUM(args[0]));
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, is_char) {
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(VAL_IS_CHAR(args[0]));
  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, apply) {
  (void) self;
  GS_FAIL_IF(argc < 2, "Not enough arguments", NULL);

  Val firstArg = args[0];
  TypeIdx ti;
  if (!VAL_IS_GC_PTR(firstArg) ||
      (ti = gs_gc_typeinfo(VAL2PTR(u8, firstArg)),
       ti != NATIVE_CLOSURE_TYPE && ti != INTERP_CLOSURE_TYPE)) {
    GS_FAILWITH("Not a function", NULL);
  }

  u16 totalArgc = argc - 2;
  Val arglist = args[argc - 1];
  while (VAL_IS_GC_PTR(arglist) && gs_gc_typeinfo(VAL2PTR(u8, arglist)) == CONS_TYPE) {
    GS_FAIL_IF(totalArgc == (u16)-1, "Integer overflow", NULL);
    totalArgc++;
    arglist = PTR_REF(Cons, VAL2PTR(u8, arglist)).cdr;
  }
  GS_FAIL_IF(arglist != VAL_NIL, "Last argument not a list", NULL);

  Val *buf = gs_alloc(GS_ALLOC_META(Val, totalArgc));
  memcpy(buf, args + 1, (argc - 2) * sizeof(Val));

  u8 *arglistP = VAL2PTR(u8, args[argc - 1]);
  Val *it = buf + (argc - 2);
  Val *end = buf + totalArgc;
  for (; it != end; ++it) {
    *it = PTR_REF(Cons, arglistP).car;
    arglistP = VAL2PTR(u8, PTR_REF(Cons, arglistP).cdr);
  }

  gs_call(&PTR_REF(Closure, VAL2PTR(u8, firstArg)), totalArgc, buf, retc, rets);

  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, box) {
  GS_CHECK_ARITY(1, 1);
  anyptr val;
  GS_TRY(gs_gc_alloc(BOX_TYPE, &val));
  PTR_REF(Box, val).value = args[0];
  rets[0] = PTR2VAL_GC(val);
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, unbox) {
  GS_CHECK_ARITY(1, 1);
  Val arg = args[0];
  anyptr val;
  GS_FAIL_IF(!VAL_IS_GC_PTR(arg) || gs_gc_typeinfo(val = VAL2PTR(u8, arg)) != BOX_TYPE, "Not a box", NULL);
  rets[0] = PTR_REF(Box, val).value;
  GS_RET_OK;
}
GS_TOP_CLOSURE(STATIC, box_set) {
  GS_CHECK_ARITY(2, 1);
  Val arg = args[0];
  Val toWrite = args[1];
  anyptr val;
  GS_FAIL_IF(!VAL_IS_GC_PTR(arg) || gs_gc_typeinfo(val = VAL2PTR(u8, arg)) != BOX_TYPE, "Not a box", NULL);
  Val *target = &PTR_REF(Box, val).value;
  if (VAL_IS_GC_PTR(toWrite)) {
    gs_gc_write_barrier(val, target, VAL2PTR(u8, toWrite), FieldGcTagged);
  }
  rets[0] = *target = toWrite;
  GS_RET_OK;
}

Err *gs_add_primitives() {
  GS_FAIL_IF(!gs_global_gc, "No garbage collector", NULL);

  Symbol *sym;
  NativeClosure *cls;

#define ADD(SYM, CLS)                                                   \
  do {                                                                  \
    GS_TRY(gs_intern(GS_UTF8_CSTR(SYM), &sym));                         \
    GS_TRY(gs_gc_alloc(NATIVE_CLOSURE_TYPE, (anyptr *)&cls));           \
    cls->parent = (CLS);                                                \
    sym->value = PTR2VAL_GC(cls);                                       \
  } while(0)

  ADD("symbol-set-value!", symbol_set_value);
  ADD("symbol-set-macro!", symbol_set_macro);
  ADD("cons", cons);
  ADD("car", car);
  ADD("cdr", cdr);
  ADD("eq?", is_eq);
  ADD("list?", is_list);
  ADD("symbol?", is_symbol);
  ADD("string?", is_string);
  ADD("bytestring?", is_bytestring);
  ADD("number?", is_number);
  ADD("char?", is_char);
  ADD("program-args", program_args);
  ADD("apply", apply);
  ADD("box", box);
  ADD("unbox", unbox);
  ADD("box-set", box_set);

#undef ADD

  GS_RET_OK;
}

Err *gs_add_primitive_types() {
  GS_FAIL_IF(!gs_global_gc, "No garbage collector", NULL);

  TypeIdx idx;

#define ADD(Type, CAPS)                                     \
  do {                                                      \
    GS_TRY(gs_gc_push_type(Type##_INFO, &idx));             \
    GS_FAIL_IF(idx != CAPS##_TYPE, "Wrong type index", NULL);   \
  } while(0)

  ADD(Symbol, SYMBOL);
  ADD(InlineUtf8Str, STRING);
  ADD(InlineBytes, BYTESTRING);
  ADD(Cons, CONS);
  ADD(RawArray, RAW_ARRAY);
  ADD(Array, ARRAY);
  ADD(OpaqueArray, OPAQUE_ARRAY);
  ADD(WordArray, WORD_ARRAY);
  ADD(NativeClosure, NATIVE_CLOSURE);
  ADD(InterpClosure, INTERP_CLOSURE);
  ADD(SymTable, SYM_TABLE);
  ADD(SymTableBucket, SYM_TABLE_BUCKET);
  ADD(Image, IMAGE);
  ADD(Box, BOX);

#undef ADD

  GS_RET_OK;
}
