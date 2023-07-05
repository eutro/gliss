#pragma once

#include <stdalign.h>
#include <string.h>

#include "rt_prims.h"

#define GS_C_ALLOC

typedef struct Bytes {
  u8 *bytes;
  u32 len;
} Bytes;

typedef Bytes Utf8Str;

#include "gc/gc_type.h"

#define GS_UTF8_CSTR(MSG) ((Utf8Str) { (u8 *) (MSG), sizeof(MSG) - 1 })
#define GS_UTF8_CSTR_DYN(MSG) ((Utf8Str) { (u8 *) (MSG), strlen(MSG) })

typedef struct Err Err;

#ifndef GS_ERR_MAX_TRACE
#define GS_ERR_MAX_TRACE 512
#endif

typedef struct ErrFrame {
  Utf8Str msg;
  Utf8Str func;
  Utf8Str file;
  u32 line;
} ErrFrame;

struct Err {
  Val exn;
  u32 len;
  ErrFrame frames[GS_ERR_MAX_TRACE];
};
extern Err gs_current_err;

#define GS_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define GS_ERR_FRAME(MSG, FUNC, FILE, LINE)     \
  ((ErrFrame) {                                 \
    .msg = (MSG),                               \
    .func = (FUNC),                             \
    .file = (FILE),                             \
    .line = (LINE) })
#define GS_ERR_HERE(MSG)                        \
  ((ErrFrame) {                                 \
    .msg = GS_UTF8_CSTR_DYN(MSG),               \
    .func = GS_UTF8_CSTR(__func__),             \
    .file = GS_UTF8_CSTR_DYN(GS_FILENAME),      \
    .line = __LINE__ })

#define GS_FAIL_HERE_DEFAULT(X) return (X)
#define GS_FAIL_HERE(X) GS_FAIL_HERE_DEFAULT(X)
#define GS_FAILWITH_FRAME(FRAME, EXN, CAUSE)                            \
  do {                                                                  \
    if (!CAUSE) {                                                       \
      gs_current_err.len = 0;                                           \
      gs_current_err.exn = (EXN);                                       \
    }                                                                   \
    if (gs_current_err.len < GS_ERR_MAX_TRACE) {                        \
      gs_current_err.frames[gs_current_err.len] = (FRAME);              \
    }                                                                   \
    gs_current_err.len++;                                               \
    GS_FAIL_HERE(&gs_current_err);                                      \
  } while(0)
#define GS_FAILWITH(MSG, CAUSE) GS_FAILWITH_FRAME(GS_ERR_HERE(MSG), PTR2VAL_NOGC(NULL), CAUSE)
#define GS_FAILWITH_VAL_MSG(MSG, VAL) GS_FAILWITH_FRAME(GS_ERR_HERE(MSG), VAL, NULL)
#define GS_FAILWITH_VAL(VAL)                    \
  do {                                          \
    gs_current_err.len = 0;                     \
    gs_current_err.exn = (VAL);                 \
    return &gs_current_err;                     \
  } while(0)

#define NO_CLEANUP while(0)

#define GS_FAIL_IF_C(COND, MSG, CAUSE, CLEANUP) \
  do {                                          \
    if (COND) {                                 \
      CLEANUP;                                  \
      GS_FAILWITH(MSG, CAUSE);                  \
    }                                           \
  } while(0)
#define GS_FAIL_IF(COND, MSG, CAUSE) GS_FAIL_IF_C(COND, MSG, CAUSE, NO_CLEANUP)

#define GS_TRY_MSG_C(CALL, MSG, CLEANUP)                                \
  do {                                                                  \
    Err *gs_try_res = (CALL);                                           \
    GS_FAIL_IF_C(gs_try_res, MSG, gs_try_res, CLEANUP);                 \
  } while(0)
#define GS_TRY_MSG(CALL, MSG) GS_TRY_MSG_C(CALL, MSG, NO_CLEANUP)

#define GS_TRY_C(CALL, CLEANUP) GS_TRY_MSG_C(CALL, #CALL, CLEANUP)
#define GS_TRY(CALL) GS_TRY_MSG_C(CALL, #CALL, NO_CLEANUP)

#define GS_RET_OK return NULL

#define VAL_TAG(VAL) ((VAL) & 3)
#define VAL_IS_TAG(VAL, TAG) (VAL_TAG(VAL) == (TAG))

#define VAL_IS_FIXNUM(VAL) VAL_IS_TAG(VAL, 0)
#define VAL2UFIX(VAL) ((ufix) (VAL) >> 2)
#define VAL2SFIX(VAL) ((ifix) (VAL) >> 2)
#define FIX2VAL(FIX) ((u64) (FIX) << 2)

#define VAL_IS_PTR(VAL) (((VAL) & 1) != 0)
#define VAL_IS_GC_PTR(VAL) VAL_IS_TAG(VAL, 1)
#define VAL_IS_NOGC_PTR(VAL) VAL_IS_TAG(VAL, 3)
#define VAL2PTR(TY, VAL) ((TY *) ((uptr) (VAL) & ~3))
#define PTR2VAL_GC(PTR) ((Val) ((uptr) (PTR) | 1))
#define PTR2VAL_NOGC(PTR) ((Val) ((uptr) (PTR) | 3))

#define VAL_IS_CONST(VAL) VAL_IS_TAG(VAL, 2)
#define VAL_TRUTHY(VAL) (((VAL) & 7) != 6)
#define VAL_FALSY(VAL) (((VAL) & 7) == 6)
#define VAL_FALSE ((Val) 0x0E) // 0b1110
#define VAL_TRUE ((Val) 0x0A) // 0b1010
#define BOOL2VAL(X) ((X) ? VAL_TRUE : VAL_FALSE)
#define VAL_NIL ((Val) 0x06) // 0b0110
#define VAL_EOF ((Val) 0x08) // 0b1000

#define VAL_IS_CHAR(VAL) (VAL_IS_CONST(VAL) && (((VAL) & 0xFFFFFFFC) == 0))
#define VAL2CHAR(VAL) ((u32) ((ufix) (VAL) >> 32))
#define CHAR2VAL(VAL) ((((Val) (VAL)) << 32) | 2)

typedef struct Closure Closure;
#define GS_CLOSURE_ARGS Closure *self, u16 argc, Val *args, u16 retc, Val *rets
typedef Err *(*ClosureFn)(GS_CLOSURE_ARGS);
struct Closure {
  ClosureFn call;
  // extra data
};
DEFINE_GC_TYPE(
  NativeClosure,
  NOGC(FIX), Closure, parent,
  NOGC(FIX), u32, len,
  GC(RSZ(len), Tagged), ValArray, captured
);
Err *gs_call(GS_CLOSURE_ARGS);

#define GS_CHECK_ARG_ARITY(ARGS)                                     \
  GS_FAIL_IF(argc != (ARGS), "bad argument arity, expected: " #ARGS, NULL)
#define GS_CHECK_RET_ARITY(RETS)                                     \
  GS_FAIL_IF(retc != (RETS), "bad return arity, expected: " #RETS, NULL)
#define GS_CHECK_ARITY(ARGS, RETS) \
  do {                                                  \
    GS_CHECK_ARG_ARITY(ARGS);                           \
    GS_CHECK_RET_ARITY(RETS);                           \
    (void) self; (void) rets; (void) args;              \
  } while(0)

#define GS_VIS_PUBLIC
#define GS_VIS_STATIC static
#define GS_CLOSURE(NAME) Err *NAME(GS_CLOSURE_ARGS)
#define GS_TOP_CLOSURE(VIS, NAME)                    \
  static Err *NAME##_impl(GS_CLOSURE_ARGS);          \
  GS_VIS_##VIS Closure NAME = { NAME##_impl };       \
  static Err *NAME##_impl(GS_CLOSURE_ARGS)

void gs_write_error(Err *err);

typedef struct AllocMeta {
  u32 size;
  u32 align;
  u32 count;
} AllocMeta;

typedef struct Allocator Allocator;
typedef struct AllocatorVt {
  anyptr (*alloc)(Allocator *self, AllocMeta meta);
  anyptr (*realloc)(Allocator *self, anyptr ptr, AllocMeta oldMeta, AllocMeta newMeta);
  void (*free)(Allocator *self, anyptr ptr, AllocMeta meta);
} AllocatorVt;

struct Allocator {
  AllocatorVt *table;
};

#ifdef GS_C_ALLOC
extern Allocator gs_c_alloc;
#endif

extern Allocator *gs_current_alloc;

#define GS_WITH_ALLOC(ALLOC)                                            \
  for (struct { Allocator *old; int i; } gs_wa_loop = {gs_current_alloc, 0}; \
       gs_wa_loop.i == 0                                                \
         ? (gs_current_alloc = (ALLOC), 1)                              \
         : (gs_current_alloc = gs_wa_loop.old, 0);                      \
       gs_wa_loop.i++)

#define GS_ALLOC_ALIGN_SIZE(ALIGN, SIZE, COUNT) ((AllocMeta) { (SIZE), (ALIGN), (COUNT) })
#define GS_ALLOC_META(TYPE, COUNT) GS_ALLOC_ALIGN_SIZE(alignof(TYPE), sizeof(TYPE), COUNT)

static inline anyptr gs_alloc(AllocMeta meta) {
  return gs_current_alloc->table->alloc(gs_current_alloc, meta);
}

static inline anyptr gs_realloc(anyptr ptr, AllocMeta oldMeta, AllocMeta newMeta) {
  return gs_current_alloc->table->realloc(gs_current_alloc, ptr, oldMeta, newMeta);
}

static inline void gs_free(anyptr ptr, AllocMeta meta) {
  return gs_current_alloc->table->free(gs_current_alloc, ptr, meta);
}

typedef u8 u8s[1];
DEFINE_GC_TYPE(
  InlineBytes,
  NOGC(FIX), u32, len,
  NOGC(RSZ(len)), u8s, bytes
);
DEFINE_GC_TYPE(
  InlineUtf8Str,
  NOGC(FIX), u32, len,
  NOGC(RSZ(len)), u8s, bytes
);
#define GS_DECAY_BYTES(VAL) ((Bytes) { (VAL)->bytes, (VAL)->len })

DEFINE_GC_TYPE(
  Symbol,
  NOGC(FIX), Closure, fn,
  GC(FIX, Tagged), Val, value,
  GC(FIX, Raw), InlineUtf8Str *, name,
  NOGC(FIX), bool, isMacro
);

typedef Symbol *SymbolArray[1];
DEFINE_GC_TYPE(
  SymTableBucket,
  NOGC(FIX), u32, len,
  NOGC(FIX), u32, cap,
  GC(RSZ(cap), Raw), SymbolArray, syms
);
typedef SymTableBucket *SymTableBuckets[1];
DEFINE_GC_TYPE(
  SymTable,
  NOGC(FIX), u32, bucketc,
  NOGC(FIX), u32, size,
  GC(RSZ(bucketc), Raw), SymTableBuckets, buckets
);

extern SymTable *gs_global_syms;
extern Closure symbol_invoke_closure;

u64 gs_hash_bytes(Bytes bytes);
int gs_bytes_cmp(Bytes lhs, Bytes rhs);

Err *gs_alloc_sym_table(void);
Err *gs_intern(Utf8Str name, Symbol **out);
Symbol *gs_reverse_lookup(Val value);
