#ifndef IMPL
#include "primitives_impl.h"
#define IMPL(_nm, cname, ...) GS_TOP_CLOSURE(STATIC, cname) __VA_ARGS__
#endif

IMPL("symbol-set-value!", symbol_set_value, {
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
  });
IMPL("symbol-set-macro!", symbol_set_macro, {
    GS_CHECK_ARITY(2, 1);
    GS_FAIL_IF(!is_symbol0(args[0]), "Not a symbol", NULL);
    Symbol *sym = VAL2PTR(Symbol, args[0]);
    sym->isMacro = VAL_TRUTHY(args[1]);
    rets[0] = args[0];
    GS_RET_OK;
  });

IMPL("cons", cons, {
    GS_CHECK_ARITY(2, 1);
    anyptr pair;
    GS_FAIL_IF(!is_list0(args[1]), "Attempted to create improper list", NULL);
    GS_TRY(gs_gc_alloc(CONS_TYPE, &pair));
    memcpy(pair, args, 2 * sizeof(Val));
    rets[0] = PTR2VAL_GC(pair);
    GS_RET_OK;
  });
IMPL("car", car, {
    GS_CHECK_ARITY(1, 1);
    GS_FAIL_IF(!is_type(args[0], CONS_TYPE), "Not a pair", NULL);
    u8 *raw = VAL2PTR(u8, args[0]);
    memcpy(rets, (Val *)raw, sizeof(Val));
    GS_RET_OK;
  });
IMPL("cdr", cdr, {
    GS_CHECK_ARITY(1, 1);
    u8 *raw = VAL2PTR(u8, args[0]);
    GS_FAIL_IF(!is_type(args[0], CONS_TYPE), "Not a pair", NULL);
    memcpy(rets, (Val *)raw + 1, sizeof(Val));
    GS_RET_OK;
  });

IMPL("program-args", program_args, {
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
  });

IMPL("eq?", is_eq, {
    GS_CHECK_ARITY(2, 1);
    rets[0] = BOOL2VAL(args[0] == args[1]);
    GS_RET_OK;
  });
IMPL("list?", is_list, {
    GS_CHECK_ARITY(1, 1);
    rets[0] = BOOL2VAL(is_list0(args[0]));
    GS_RET_OK;
  });
IMPL("string?", is_string, {
    GS_CHECK_ARITY(1, 1);
    rets[0] = BOOL2VAL(is_type(args[0], STRING_TYPE));
    GS_RET_OK;
  });
IMPL("bytestring?", is_bytestring, {
    GS_CHECK_ARITY(1, 1);
    rets[0] = BOOL2VAL(is_type(args[0], BYTESTRING_TYPE));
    GS_RET_OK;
  });
IMPL("symbol?", is_symbol, {
    GS_CHECK_ARITY(1, 1);
    rets[0] = BOOL2VAL(is_symbol0(args[0]));
    GS_RET_OK;
  });
IMPL("number?", is_number, {
    GS_CHECK_ARITY(1, 1);
    rets[0] = BOOL2VAL(VAL_IS_FIXNUM(args[0]));
    GS_RET_OK;
  });
IMPL("char?", is_char, {
    GS_CHECK_ARITY(1, 1);
    rets[0] = BOOL2VAL(VAL_IS_CHAR(args[0]));
    GS_RET_OK;
  });

IMPL("apply", apply, {
    (void) self;
    GS_FAIL_IF(argc < 2, "Not enough arguments", NULL);

    Val firstArg = args[0];
    GS_FAIL_IF(!is_callable(firstArg), "Not a function", NULL);

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
  });

IMPL("box", box, {
    GS_CHECK_ARITY(1, 1);
    anyptr val;
    GS_TRY(gs_gc_alloc(BOX_TYPE, &val));
    PTR_REF(Box, val).value = args[0];
    rets[0] = PTR2VAL_GC(val);
    GS_RET_OK;
  });
IMPL("unbox", unbox, {
    GS_CHECK_ARITY(1, 1);
    Val arg = args[0];
    anyptr val;
    GS_FAIL_IF(!VAL_IS_GC_PTR(arg) || gs_gc_typeinfo(val = VAL2PTR(u8, arg)) != BOX_TYPE, "Not a box", NULL);
    rets[0] = PTR_REF(Box, val).value;
    GS_RET_OK;
  });
IMPL("box-set!", box_set, {
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
  });

IMPL("new-bytestring", new_bytestring, {
    GS_CHECK_ARITY(1, 1);
    Val arg = args[0];
    GS_FAIL_IF(!VAL_IS_FIXNUM(arg), "Not a number", NULL);
    u32 len = VAL2UFIX(arg);
    anyptr ret;
    GS_TRY(gs_gc_alloc_array(BYTESTRING_TYPE, len, &ret));
    memset(PTR_REF(InlineBytes, ret).bytes, 0, len);
    rets[0] = PTR2VAL_GC(ret);
    GS_RET_OK;
  });
