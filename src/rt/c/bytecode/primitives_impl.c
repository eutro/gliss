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

#include "primitives.h"
#ifndef IMPL
#include "primitives_impl.h"
#define IMPL(_nm, cname) GS_TOP_CLOSURE(STATIC, cname)
#define EMIT 1
#endif

#define RAISE_LIST(MSG, ...)                                            \
  do {                                                                  \
    Val raise_list_ls[] = { __VA_ARGS__ };                              \
    Val raise_list_exn;                                                 \
    GS_TRY(gs_alloc_list(raise_list_ls, sizeof(raise_list_ls) / sizeof(Val), &raise_list_exn)); \
    GS_TRY_MSG(gs_call(&raise, 1, &raise_list_exn, 0, NULL), MSG);      \
  } while(0)
#define FAIL_IF_LIST(COND, MSG, ...)            \
  do {                                          \
    if (COND) {                                 \
      RAISE_LIST(MSG, __VA_ARGS__);             \
    }                                           \
  } while(0)

IMPL("raise", raise)
#if EMIT
{
  (void)self; (void)retc, (void)rets;
  GS_CHECK_ARG_ARITY(1);
  GS_FAIL_IF(VAL_IS_PTR(args[0]) && VAL2PTR(u8, args[0]) == NULL, "Cannot raise null", NULL);
  GS_FAILWITH_VAL(args[0]);
}
#endif

IMPL("symbol-set-value!", symbol_set_value)
#if EMIT
{
  GS_CHECK_ARITY(2, 1);
  GS_FAIL_IF(!is_symbol0(args[0]), "Not a symbol", NULL);
  Symbol *sym = VAL2PTR(Symbol, args[0]);
  LOG_TRACE("Setting symbol: %.*s", sym->name->len, sym->name->bytes);
  Val toWrite = args[1];
  if (VAL_IS_GC_PTR(toWrite)) {
    GS_TRY(gs_gc_write_barrier(sym, &sym->value, VAL2PTR(u8, toWrite), FieldGcTagged));
    if (is_type(toWrite, INTERP_CLOSURE_TYPE)) {
      InterpClosure *twCls = VAL2PTR(InterpClosure, toWrite);
      if (twCls->assignedTo == NULL) {
        GS_TRY(gs_gc_write_barrier(twCls, &twCls->assignedTo, sym, FieldGcRaw));
        twCls->assignedTo = sym;
      }
    }
  }
  sym->value = toWrite;
  rets[0] = args[0];
  GS_RET_OK;
}
#endif
IMPL("symbol-set-macro!", symbol_set_macro)
#if EMIT
{
  GS_CHECK_ARITY(2, 1);
  GS_FAIL_IF(!is_symbol0(args[0]), "Not a symbol", NULL);
  Symbol *sym = VAL2PTR(Symbol, args[0]);
  sym->isMacro = VAL_TRUTHY(args[1]);
  rets[0] = args[0];
  GS_RET_OK;
}
#endif

IMPL("string->bytestring", string_to_bytestring)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  GS_FAIL_IF(!is_type(args[0], STRING_TYPE), "Not a string", NULL);
  InlineUtf8Str *str = VAL2PTR(InlineUtf8Str, args[0]);
  InlineBytes *bs;
  GS_TRY(gs_gc_alloc_array(BYTESTRING_TYPE, str->len, (anyptr *)&bs));
  memcpy(bs->bytes, str->bytes, str->len);
  rets[0] = PTR2VAL_GC(bs);
  GS_RET_OK;
}
#endif

IMPL("symbol->bytestring", symbol_to_bytestring)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  GS_FAIL_IF(!is_symbol0(args[0]), "Not a symbol", NULL);
  Symbol *sym = VAL2PTR(Symbol, args[0]);
  InlineBytes *bs;
  GS_TRY(gs_gc_alloc_array(BYTESTRING_TYPE, sym->name->len, (anyptr *)&bs));
  memcpy(bs->bytes, sym->name->bytes, sym->name->len);
  rets[0] = PTR2VAL_GC(bs);
  GS_RET_OK;
}
#endif

IMPL("cons", cons)
#if EMIT
{
  GS_CHECK_ARITY(2, 1);
  anyptr pair;
  GS_FAIL_IF(!is_list0(args[1]), "Attempted to create improper list", NULL);
  GS_TRY(gs_gc_alloc(CONS_TYPE, &pair));
  memcpy(pair, args, 2 * sizeof(Val));
  rets[0] = PTR2VAL_GC(pair);
  GS_RET_OK;
}
#endif
IMPL("car", car)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  FAIL_IF_LIST(!is_type(args[0], CONS_TYPE), "Not a pair", args[0]);
  u8 *raw = VAL2PTR(u8, args[0]);
  memcpy(rets, (Val *)raw, sizeof(Val));
  GS_RET_OK;
}
#endif
IMPL("cdr", cdr)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  u8 *raw = VAL2PTR(u8, args[0]);
  FAIL_IF_LIST(!is_type(args[0], CONS_TYPE), "Not a pair", args[0]);
  memcpy(rets, (Val *)raw + 1, sizeof(Val));
  GS_RET_OK;
}
#endif

IMPL("program-args", program_args)
#if EMIT
{
  GS_CHECK_ARITY(0, 1);
  const char **end = gs_argv;
  const char **it = end + gs_argc - 1;
  Val ret = VAL_NIL;
  for (; it != end; --it) {
    u32 len = strlen(*it);
    InlineUtf8Str *argStr;
    GS_TRY(gs_gc_alloc_array(STRING_TYPE, len, (anyptr *)&argStr));
    memcpy(argStr->bytes, *it, len);
    Val args[] = { PTR2VAL_GC(argStr), ret };
    GS_TRY(gs_call(&cons, 2, args, 1, &ret));
  }
  rets[0] = ret;
  GS_RET_OK;
}
#endif

IMPL("eq?", is_eq)
#if EMIT
{
  GS_CHECK_ARITY(2, 1);
  rets[0] = BOOL2VAL(args[0] == args[1]);
  GS_RET_OK;
}
#endif
IMPL("list?", is_list)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(is_list0(args[0]));
  GS_RET_OK;
}
#endif
IMPL("string?", is_string)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(is_type(args[0], STRING_TYPE));
  GS_RET_OK;
}
#endif
IMPL("bytestring?", is_bytestring)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(is_type(args[0], BYTESTRING_TYPE));
  GS_RET_OK;
}
#endif
IMPL("symbol?", is_symbol)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(is_symbol0(args[0]));
  GS_RET_OK;
}
#endif
IMPL("number?", is_number)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(VAL_IS_FIXNUM(args[0]));
  GS_RET_OK;
}
#endif
IMPL("char?", is_char)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  rets[0] = BOOL2VAL(VAL_IS_CHAR(args[0]));
  GS_RET_OK;
}
#endif

IMPL("apply", apply)
#if EMIT
{
  (void) self;
  GS_FAIL_IF(argc < 2, "Not enough arguments", NULL);

  Val firstArg = args[0];
  FAIL_IF_LIST(!is_callable(firstArg), "Not a function", args[0]);

  u16 totalArgc = argc - 2;
  Val arglist = args[argc - 1];
  FAIL_IF_LIST(!is_list0(arglist), "Not a list", arglist);
  while (arglist != VAL_NIL /* && gs_gc_typeinfo(VAL2PTR(u8, arglist)) == CONS_TYPE (guaranteed by list-ness) */) {
    GS_FAIL_IF(totalArgc == (u16) -1, "Integer overflow", NULL);
    totalArgc++;
    arglist = PTR_REF(Cons, VAL2PTR(u8, arglist)).cdr;
  }

  // maybe alloca instead?
  Val *buf = gs_alloc(GS_ALLOC_META(Val, totalArgc));
  memcpy(buf, args + 1, (argc - 2) * sizeof(Val));

  u8 *arglistP = VAL2PTR(u8, args[argc - 1]);
  Val *it = buf + (argc - 2);
  Val *end = buf + totalArgc;
  for (; it != end; ++it) {
    *it = PTR_REF(Cons, arglistP).car;
    arglistP = VAL2PTR(u8, PTR_REF(Cons, arglistP).cdr);
  }

  Err *res = gs_call(&PTR_REF(Closure, VAL2PTR(u8, firstArg)), totalArgc, buf, retc, rets);
  gs_free(buf, GS_ALLOC_META(Val, totalArgc));
  return res;
}
#endif

IMPL("box", box)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  anyptr val;
  GS_TRY(gs_gc_alloc(BOX_TYPE, &val));
  PTR_REF(Box, val).value = args[0];
  rets[0] = PTR2VAL_GC(val);
  GS_RET_OK;
}
#endif
IMPL("unbox", unbox)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val arg = args[0];
  anyptr val;
  FAIL_IF_LIST(!VAL_IS_GC_PTR(arg) || gs_gc_typeinfo(val = VAL2PTR(u8, arg)) != BOX_TYPE, "Not a box", arg);
  rets[0] = PTR_REF(Box, val).value;
  GS_RET_OK;
}
#endif
IMPL("box-set!", box_set)
#if EMIT
{
  GS_CHECK_ARITY(2, 1);
  Val arg = args[0];
  Val toWrite = args[1];
  anyptr val;
  GS_FAIL_IF(!VAL_IS_GC_PTR(arg) || gs_gc_typeinfo(val = VAL2PTR(u8, arg)) != BOX_TYPE, "Not a box", NULL);
  Val *target = &PTR_REF(Box, val).value;
  if (VAL_IS_GC_PTR(toWrite)) {
    GS_TRY(gs_gc_write_barrier(val, target, VAL2PTR(u8, toWrite), FieldGcTagged));
  }
  rets[0] = *target = toWrite;
  GS_RET_OK;
}
#endif

IMPL("new-bytestring", new_bytestring)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val arg = args[0];
  GS_FAIL_IF(!VAL_IS_FIXNUM(arg), "Not a number", NULL);
  u32 len = VAL2UFIX(arg);
  anyptr ret;
  GS_TRY(gs_gc_alloc_array(BYTESTRING_TYPE, len, &ret));
  memset(PTR_REF(InlineBytes, ret).bytes, 0, len);
  rets[0] = PTR2VAL_GC(ret);
  GS_RET_OK;
}
#endif

#define STRING_LENGTH_REF(LOWER_NAME, UPPER_NAME, TYPE_NAME, WRAP)  \
  IMPL(#LOWER_NAME "-length", LOWER_NAME##_length)                  \
  EMIT_STRING_LENGTH(LOWER_NAME, UPPER_NAME, TYPE_NAME)             \
  IMPL(#LOWER_NAME "-ref", LOWER_NAME##_ref)                        \
  EMIT_STRING_REF(LOWER_NAME, UPPER_NAME, TYPE_NAME, WRAP)

#if EMIT
#  define EMIT_STRING_LENGTH(LOWER_NAME, UPPER_NAME, TYPE_NAME)         \
  {                                                                     \
    GS_CHECK_ARITY(1, 1);                                               \
    Val it = args[0];                                                   \
    GS_FAIL_IF(!is_type(it, UPPER_NAME##_TYPE), "Not a " #LOWER_NAME, NULL); \
    rets[0] = FIX2VAL(VAL2PTR(TYPE_NAME, it)->len);                     \
    GS_RET_OK;                                                          \
  }
#  define EMIT_STRING_REF(LOWER_NAME, UPPER_NAME, TYPE_NAME, WRAP)      \
  {                                                                     \
    GS_CHECK_ARITY(2, 1);                                               \
    Val it = args[0];                                                   \
    Val idx = args[1];                                                  \
    GS_FAIL_IF(!is_type(it, UPPER_NAME##_TYPE), "Not a " #LOWER_NAME, NULL); \
    GS_FAIL_IF(!VAL_IS_FIXNUM(idx), "Not a number", NULL);              \
    TYPE_NAME *itV = VAL2PTR(TYPE_NAME, it);                            \
    u64 idxV = VAL2UFIX(idx);                                           \
    GS_FAIL_IF(idxV >= itV->len, "Index out of bounds", NULL);          \
    u8 byte = itV->bytes[idxV];                                         \
    rets[0] = WRAP(byte);                                               \
    GS_RET_OK;                                                          \
  }
#else
#  define EMIT_STRING_LENGTH(_1, _2, _3)
#  define EMIT_STRING_REF(_1, _2, _3, _4)
#endif

STRING_LENGTH_REF(bytestring, BYTESTRING, InlineBytes, FIX2VAL);
STRING_LENGTH_REF(string, STRING, InlineUtf8Str, CHAR2VAL);

#undef STRING_LENGTH_REF
#undef EMIT_STRING_LENGTH
#undef EMIT_STRING_REF

IMPL("bytestring-set!", bytestring_set)
#if EMIT
{
  GS_CHECK_ARITY(3, 1);
  Val bs = args[0];
  Val idx = args[1];
  Val value = args[2];
  FAIL_IF_LIST(!is_type(bs, BYTESTRING_TYPE), "Not a bytestring", bs, idx, value);
  FAIL_IF_LIST(!VAL_IS_FIXNUM(idx), "Not a number", bs, idx, value);
  FAIL_IF_LIST(!VAL_IS_FIXNUM(value), "Not a number", bs, idx, value);
  InlineBytes *bsP = VAL2PTR(InlineBytes, bs);
  u64 idxV = VAL2UFIX(idx);
  u64 valueB = VAL2UFIX(value);
  FAIL_IF_LIST(idxV >= bsP->len, "Index out of bounds", bs, idx, value);
  bsP->bytes[idxV] = (u8) valueB;
  rets[0] = VAL_NIL;
  GS_RET_OK;
}
#endif

IMPL("bytestring-copy!", bytestring_copy)
#if EMIT
{
  GS_CHECK_ARITY(5, 1);

  Val dst = args[0];
  Val dstStart = args[1];
  Val src = args[2];
  Val srcStart = args[3];
  Val len = args[4];
  GS_FAIL_IF(!is_type(dst, BYTESTRING_TYPE), "Not a bytestring", NULL);
  GS_FAIL_IF(!VAL_IS_FIXNUM(dstStart), "Not a number", NULL);
  GS_FAIL_IF(!is_type(src, BYTESTRING_TYPE), "Not a bytestring", NULL);
  GS_FAIL_IF(!VAL_IS_FIXNUM(srcStart), "Not a number", NULL);
  GS_FAIL_IF(!VAL_IS_FIXNUM(len), "Not a number", NULL);

  InlineBytes *dstP = VAL2PTR(InlineBytes, dst);
  InlineBytes *srcP = VAL2PTR(InlineBytes, src);
  u64 dstStartV = VAL2UFIX(dstStart);
  u64 srcStartV = VAL2UFIX(srcStart);
  u64 lenV = VAL2UFIX(len);
  FAIL_IF_LIST(dstStartV + lenV > dstP->len, "Destination region out of range", dst, dstStart, src, srcStart, len);
  FAIL_IF_LIST(srcStartV + lenV > srcP->len, "Source region out of range", dst, dstStart, src, srcStart, len);
  memcpy(dstP->bytes + dstStartV, srcP->bytes + srcStartV, lenV);

  rets[0] = VAL_NIL;
  GS_RET_OK;
}
#endif

IMPL("list->string", list_to_string)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val list = args[0];
  FAIL_IF_LIST(!is_list0(list), "Not a list", list);
  u32 len = 0;
  {
    Val counting = list;
    while (counting != VAL_NIL) {
      Cons *pair = VAL2PTR(Cons, counting);
      GS_FAIL_IF(!VAL_IS_CHAR(pair->car), "Not a char", NULL);
      counting = pair->cdr;
      ++len; // TODO utf-8
    }
  }
  InlineUtf8Str *str;
  GS_TRY(gs_gc_alloc_array(STRING_TYPE, len, (anyptr *)&str));
  {
    Val copying = list;
    u32 i = 0;
    while (copying != VAL_NIL) {
      Cons *pair = VAL2PTR(Cons, copying);
      str->bytes[i] = (u8) VAL2CHAR(pair->car);
      copying = pair->cdr;
      ++i;
    }
  }
  rets[0] = PTR2VAL_GC(str);
  GS_RET_OK;
}
#endif

IMPL("substring", substring)
#if EMIT
{
  (void) self;
  GS_CHECK_RET_ARITY(1);
  GS_FAIL_IF(argc < 2 || argc > 3, "Bad arity", NULL);
  Val str = args[0];
  Val start = args[1];
  FAIL_IF_LIST(!is_type(str, STRING_TYPE), "Not a string", str);
  FAIL_IF_LIST(!VAL_IS_FIXNUM(start), "Not a number", start);

  InlineUtf8Str *strV = VAL2PTR(InlineUtf8Str, str);
  u64 startV = VAL2UFIX(start);

  FAIL_IF_LIST(startV > strV->len, "Start index out of range", str, start);

  u64 lenV;
  if (argc == 3) {
    Val end = args[2];
    FAIL_IF_LIST(!VAL_IS_FIXNUM(end), "Not a number", end);
    lenV = VAL2UFIX(end);
    FAIL_IF_LIST(lenV > strV->len - (u32) startV, "End out of range", str, start, end);
  } else {
    lenV = strV->len - startV;
  }

  InlineUtf8Str *subs;
  GS_TRY(gs_gc_alloc_array(STRING_TYPE, (u32) lenV, (anyptr *)&subs));
  memcpy(subs->bytes, strV->bytes + startV, lenV);
  rets[0] = PTR2VAL_GC(subs);

  GS_RET_OK;
}
#endif

IMPL("string=?", string_eq)
#if EMIT
{
  GS_CHECK_ARITY(2, 1);
  Val lhs = args[0], rhs = args[1];
  FAIL_IF_LIST(!is_type(lhs, STRING_TYPE) || !is_type(rhs, STRING_TYPE), "Not a string", lhs, rhs);
  InlineUtf8Str *lhsS = VAL2PTR(InlineUtf8Str, lhs), *rhsS = VAL2PTR(InlineUtf8Str, rhs);
  rets[0] = BOOL2VAL(lhsS->len == rhsS->len && memcmp(lhsS->bytes, rhsS->bytes, lhsS->len) == 0);
  GS_RET_OK;
}
#endif

IMPL("string-prefix?", string_prefix)
#if EMIT
{
  GS_CHECK_ARITY(2, 1);
  Val str = args[0], pref = args[1];
  FAIL_IF_LIST(!is_type(str, STRING_TYPE) || !is_type(pref, STRING_TYPE), "Not a string", str, pref);
  InlineUtf8Str *strS = VAL2PTR(InlineUtf8Str, str), *prefS = VAL2PTR(InlineUtf8Str, pref);
  rets[0] = BOOL2VAL(prefS->len <= strS->len && memcmp(prefS->bytes, strS->bytes, prefS->len) == 0);
  GS_RET_OK;
}
#endif

IMPL("char->integer", char_to_int)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val c = args[0];
  FAIL_IF_LIST(!VAL_IS_CHAR(c), "Not a char", c);
  rets[0] = FIX2VAL(VAL2CHAR(c));
  GS_RET_OK;
}
#endif

#define FX_FOLD(IDENTITY, OP)                               \
  {                                                         \
    (void)self;                                             \
    GS_CHECK_RET_ARITY(1);                                  \
    i64 val = (IDENTITY);                                   \
    for (u32 i = 0; i < argc; ++i) {                        \
      Val x = args[i];                                      \
      GS_FAIL_IF(!VAL_IS_FIXNUM(x), "Not a number", NULL);  \
      val OP##= VAL2SFIX(x);                                \
    }                                                       \
    rets[0] = FIX2VAL(val);                                 \
    GS_RET_OK;                                              \
}

IMPL("+", fx_add)
#if EMIT
FX_FOLD(0, +)
#endif
IMPL("*", fx_mul)
#if EMIT
FX_FOLD(1, *)
#endif
IMPL("bitwise-and", fx_and)
#if EMIT
FX_FOLD(~0, &)
#endif
IMPL("bitwise-ior", fx_ior)
#if EMIT
FX_FOLD(0, |)
#endif
IMPL("bitwise-xor", fx_xor)
#if EMIT
FX_FOLD(0, ^)
#endif

IMPL("-", fx_sub)
#if EMIT
{
  (void)self;
  GS_CHECK_RET_ARITY(1);
  GS_FAIL_IF(argc == 0, "Not enough arguments", NULL);
  GS_FAIL_IF(!VAL_IS_FIXNUM(args[0]), "Not a number", NULL);
  i64 val = VAL2SFIX(args[0]);
  if (argc == 1) {
    val = -val;
  } else {
    for (u32 i = 1; i < argc; ++i) {
      Val x = args[i];
      GS_FAIL_IF(!VAL_IS_FIXNUM(x), "Not a number", NULL);
      val -= VAL2SFIX(x);
    }
  }
  rets[0] = FIX2VAL(val);
  GS_RET_OK;
}
#endif

#define FX_BINOP_SETUP(lhsName, rhsName)                    \
  GS_CHECK_ARITY(2, 1);                                     \
  Val lhs = args[0], rhs = args[1];                         \
  GS_FAIL_IF(!VAL_IS_FIXNUM(lhs), "Not a number", NULL);    \
  GS_FAIL_IF(!VAL_IS_FIXNUM(rhs), "Not a number", NULL);    \
  i64 lhsName = VAL2SFIX(lhs);                                  \
  i64 rhsName = VAL2SFIX(rhs);

IMPL("arithmetic-shift", fx_shift)
#if EMIT
{
  FX_BINOP_SETUP(ret, shift);
  if (shift < 0) {
    ret >>= -shift % 64;
  } else {
    ret <<= shift % 64;
  }
  rets[0] = FIX2VAL(ret);
  GS_RET_OK;
}
#endif

IMPL("remainder", fx_rem)
#if EMIT
{
  FX_BINOP_SETUP(a, n);
  GS_FAIL_IF(n == 0, "Division by zero", NULL);
  // since C99, `a % n = a - n * (a / n)', with `a / n' truncating
  // towards zero; thus, this returns with the sign the same as `a'
  rets[0] = FIX2VAL(a % n);
  GS_RET_OK;
}
#endif
IMPL("modulo", fx_mod)
#if EMIT
{
  FX_BINOP_SETUP(a, n);
  GS_FAIL_IF(n == 0, "Division by zero", NULL);
  // we want this to return with the same sign as n, so
  i64 res;
  if ((a < 0) ^ (n < 0)) {
    // different signs

    //          vv same sign as n
    res = (n - (-a % n)) % n;
    //         ^^^^^^^^ same sign as n, but (clearly) the wrong answer
    //       ^ negation modulo n
  } else {
    // a has the same sign as n, use the easy case of C99 remainder
    res = a % n;
  }
  rets[0] = FIX2VAL(res);
  GS_RET_OK;
}
#endif

#undef FX_BINOP_SETUP

#define CMP_IMPL(SYM_NAME, CMP, CNAME) IMPL(#SYM_NAME, CNAME) CMP_BODY(CMP)
#if EMIT
#define CMP_BODY(CMP) {                                             \
    (void)self;                                                     \
    GS_CHECK_RET_ARITY(1);                                          \
    if (argc == 0) {                                                \
      rets[0] = VAL_TRUE;                                               \
    } else {                                                            \
      for (u32 i = 0; i < argc; ++i) GS_FAIL_IF(!VAL_IS_FIXNUM(args[i]), "Not a number", NULL); \
      i64 last = VAL2SFIX(args[0]);                                     \
      for (u32 i = 1; i < argc; ++i) {                                  \
        i64 next = VAL2SFIX(args[i]);                                   \
        if (!(last CMP next)) {                                         \
          rets[0] = VAL_FALSE;                                          \
          break;                                                        \
        }                                                               \
        last = next;                                                    \
      }                                                                 \
    }                                                                   \
    GS_RET_OK;                                                          \
  }
#else
#define CMP_BODY(_1)
#endif

CMP_IMPL(<, <, cmp_lt)
CMP_IMPL(<=, <=, cmp_le)
CMP_IMPL(>, >, cmp_gt)
CMP_IMPL(>=, >=, cmp_ge)
CMP_IMPL(=, ==, cmp_eq)

#undef CMP_IMPL
#undef CMP_BODY

IMPL("dbg", dbg_pr)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  pr0(stderr, args[0]);
  putc('\n', stderr);
  rets[0] = args[0];
  GS_RET_OK;
}
#endif

IMPL("dbg-suspend", dbg_suspend)
#if EMIT
{
  (void) self;
  GS_CHECK_RET_ARITY(1);
  LOG_IF_ENABLED(DEBUG, {
      LOG_DEBUG("%s", "Breakpoint hit:");
      fprintf(stderr, LOG_COLOUR_DEBUG "  args" NONE ":");
      for (u16 i = 0; i < argc; ++i) {
        fprintf(stderr, " ");
        pr0(stderr, args[i]);
      }
      fprintf(stderr, "\n");
    });
  rets[0] = VAL_NIL;
  GS_RET_OK;
}
#endif

IMPL("char-whitespace?", char_whitespace)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val c = args[0];
  GS_FAIL_IF(!VAL_IS_CHAR(c), "Not a char", NULL);
  rets[0] = BOOL2VAL(isspace(VAL2CHAR(c)));
  GS_RET_OK;
}
#endif

IMPL("intern", intern)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val str = args[0];
  GS_FAIL_IF(!is_type(str, STRING_TYPE), "Not a string", NULL);
  InlineUtf8Str *strV = VAL2PTR(InlineUtf8Str, str);
  Symbol *ret;
  GS_TRY(gs_intern(GS_DECAY_BYTES(strV), &ret));
  rets[0] = PTR2VAL_GC(ret);
  GS_RET_OK;
}
#endif

IMPL("string->number", string_to_number)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val strV = args[0];
  GS_FAIL_IF(!is_type(strV, STRING_TYPE), "Not a string", NULL);
  InlineUtf8Str *str = VAL2PTR(InlineUtf8Str, strV);

  char *it = (char *) str->bytes;
  char *end = it + str->len;
  GS_FAIL_IF(it == end, "Empty string", NULL);
  i8 sign = 1;
  if (*it == '-' || *it == '+') {
    sign = *it == '-' ? -1 : +1;
    ++it;
  }

  bool hasDigits = false;
  u64 absVal = 0;
  for (; it != end; ++it) {
    if (*it == '_') continue;
    GS_FAIL_IF(*it < '0' || *it > '9', "Invalid character for number", NULL);
    hasDigits = true;
    u64 newVal = absVal * 10 + (*it - '0');
    GS_FAIL_IF(newVal < absVal, "Integer literal too large", NULL);
    absVal = newVal;
  }

  GS_FAIL_IF(!hasDigits, "No digits", NULL);
  GS_FAIL_IF(absVal >> 63 == 1, "Integer literal too large", NULL);

  i64 retV = sign * (i64) absVal;
  rets[0] = FIX2VAL(retV);

  GS_RET_OK;
}
#endif

IMPL("symbol-macro-value", symbol_macro_value)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val sym = args[0];
  GS_FAIL_IF(!is_symbol0(sym), "Not a symbol", NULL);
  Symbol *symV = VAL2PTR(Symbol, sym);
  rets[0] = symV->isMacro ? symV->value : VAL_NIL;
  GS_RET_OK;
}
#endif

// if IO enabled

IMPL("open-file", open_file)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val file = args[0];
  GS_FAIL_IF(!is_type(file, STRING_TYPE), "Not a string", NULL);

  InlineUtf8Str *utfFile = VAL2PTR(InlineUtf8Str, file);
  char *str = gs_alloc(GS_ALLOC_META(char, utfFile->len + 1));
  GS_FAIL_IF(!str, "Failed allocation", NULL);
  memcpy(str, utfFile->bytes, utfFile->len + 1);
  str[utfFile->len] = 0;
  FILE *fp = fopen(str, "r");
  gs_free(str, GS_ALLOC_META(char, utfFile->len + 1));
  GS_FAIL_IF(!fp, "Could not open file", NULL);

  Val ret = VAL_NIL, retEnd = VAL_NIL;

  char buf[1024];

  while (true) {
    size_t len = fread(buf, 1, sizeof(buf), fp);
    if (len < sizeof(buf)) {
      if (ferror(fp)) {
        fclose(fp);
        GS_FAILWITH("IO error occured", NULL);
      }
    }

    Val front = VAL_NIL, end = VAL_NIL;
    for (char *iter = buf + len - 1; iter != buf - 1; --iter) {
      Val args[] = { CHAR2VAL(*iter), front };
      Err *err = gs_call(&cons, 2, args, 1, &front);
      if (err) {
        fclose(fp);
        GS_FAILWITH("Allocation error", err);
      }
      if (end == VAL_NIL) {
        end = front;
      }
    }
    if (retEnd == VAL_NIL) {
      ret = front;
      retEnd = end;
    } else {
      VAL2PTR(Cons, retEnd)->cdr = front;
      retEnd = end;
    }

    if (feof(fp)) break;
  }
  fclose(fp);

  return gs_call(&box, 1, &ret, 1, rets);
}
#endif

IMPL("write-file", write_file)
#if EMIT
{
  GS_CHECK_ARITY(2, 1);
  Val nameV = args[0];
  Val bytesV = args[1];
  FAIL_IF_LIST(!is_type(nameV, STRING_TYPE), "Not a string", bytesV);
  FAIL_IF_LIST(!is_type(bytesV, BYTESTRING_TYPE), "Not a bytestring", bytesV);

  InlineUtf8Str *utfFile = VAL2PTR(InlineUtf8Str, nameV);
  char *str = gs_alloc(GS_ALLOC_META(char, utfFile->len + 1));
  GS_FAIL_IF(!str, "Failed allocation", NULL);
  memcpy(str, utfFile->bytes, utfFile->len + 1);
  str[utfFile->len] = 0;
  FILE *fp = fopen(str, "w");
  gs_free(str, GS_ALLOC_META(char, utfFile->len + 1));
  GS_FAIL_IF(!fp, "Could not open file", NULL);

  InlineBytes *bytes = VAL2PTR(InlineBytes, bytesV);
  size_t written = fwrite(bytes->bytes, 1, bytes->len, fp);
  fclose(fp);
  GS_FAIL_IF(written != bytes->len, "Error writing to file", NULL);

  GS_RET_OK;
}
#endif

IMPL("dbg-dump-gc", dbg_dump_gc)
#if EMIT
{
  GS_CHECK_ARITY(0, 1);
  gs_gc_dump();
  rets[0] = VAL_NIL;
  GS_RET_OK;
}
#endif

IMPL("dbg-dump-obj", dbg_dump_obj)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  GS_FAIL_IF(!VAL_IS_GC_PTR(args[0]), "Not a GC object", NULL);
  gs_gc_dump_object(VAL2PTR(u8, args[0]));
  rets[0] = VAL_NIL;
  GS_RET_OK;
}
#endif

IMPL("call-in-new-scope", call_in_new_scope)
#if EMIT
{
  GS_FAIL_IF(argc < 1, "Not enough arguments", NULL);
  GS_FAIL_IF(!is_callable(args[0]), "Not a function", NULL);
  (void) self;
  GS_TRY(gs_gc_push_scope());
  Err *err = gs_call(VAL2PTR(Closure, args[0]), argc - 1, args + 1, retc, rets);
  if (err) {
    PUSH_DIRECT_GC_ROOTS(1, exn, &err->exn);
    GS_TRY(gs_gc_pop_scope());
    POP_GC_ROOTS(exn);
  } else {
    PUSH_DIRECT_GC_ROOTS(retc, rets, rets);
    GS_TRY(gs_gc_pop_scope());
    POP_GC_ROOTS(rets);
  }
  GS_TRY_MSG(err, "call-in-new-scope");
  GS_RET_OK;
}
#endif

IMPL("eval", eval_del)
#if EMIT
{
  (void) self;
  Symbol *sym;
  GS_TRY(gs_intern(GS_UTF8_CSTR("eval-0"), &sym));
  GS_TRY(gs_call(&sym->fn, argc, args, retc, rets));
  GS_RET_OK;
}
#endif

IMPL("index-image", index_image)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val bytesV = args[0];
  FAIL_IF_LIST(!is_type(bytesV, BYTESTRING_TYPE), "Not a bytestring", bytesV);
  InlineBytes *bs = VAL2PTR(InlineBytes, bytesV);
  Image *img;
  GS_TRY(gs_index_image(bs->len, bs->bytes, &img));
  rets[0] = PTR2VAL_GC(img);
  GS_RET_OK;
}
#endif

IMPL("new-image-closure", new_image_closure)
#if EMIT
{
  (void) self;
  GS_CHECK_RET_ARITY(1);
  GS_FAIL_IF(argc < 2, "Not enough arguments", NULL);
  Val imgV = args[0];
  Val idxV = args[1];
  FAIL_IF_LIST(!is_type(imgV, IMAGE_TYPE), "Not an image", imgV);
  FAIL_IF_LIST(!VAL_IS_FIXNUM(idxV), "Not a number", idxV);
  Image *img = VAL2PTR(Image, imgV);
  u64 idx = VAL2UFIX(idxV);
  FAIL_IF_LIST(!img->codes || idx >= img->codes->len, "Code out of range", imgV, idxV);
  InterpClosure *cls;
  GS_TRY(gs_interp_closure(img, (u32) idx, args + 2, argc - 2, &cls));
  rets[0] = PTR2VAL_GC(cls);
  GS_RET_OK;
}
#endif

IMPL("gensym", gensym)
#if EMIT
{
  GS_CHECK_ARITY(1, 1);
  Val nameV = args[0];
  GS_FAIL_IF(!is_type(nameV, STRING_TYPE), "Not a string", NULL);
  Symbol *uninterned;
  GS_TRY(gs_gc_alloc(SYMBOL_TYPE, (anyptr *)&uninterned));
  uninterned->fn = symbol_invoke_closure;
  uninterned->value = PTR2VAL_GC(uninterned);
  uninterned->name = VAL2PTR(InlineUtf8Str, nameV);
  uninterned->isMacro = false;
  rets[0] = PTR2VAL_GC(uninterned);
  GS_RET_OK;
}
#endif
