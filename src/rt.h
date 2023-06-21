#pragma once

#include <stdalign.h>

#include "rt_prims.h"

#define GS_C_STDIO
#define GS_C_ALLOC

#ifdef __cplusplus
extern "C" {
  namespace gliss {
#endif

typedef struct Bytes {
  u8 *bytes;
  u32 len;
} Bytes;

typedef Bytes Utf8Str;

#define GS_UTF8_CSTR(MSG) ((Utf8Str) { (u8 *) (MSG), sizeof(MSG) - 1 })

typedef struct Err Err;
struct Err {
  Utf8Str msg;
  Utf8Str file;
  u32 line;
  Err *cause;
};

#define GS_ERR_HERE(MSG, CAUSE) \
  ((Err) { GS_UTF8_CSTR(MSG), GS_UTF8_CSTR(__FILE__), __LINE__, (CAUSE) })

#define GS_FW_ERR1(LINE) gs_fw_err_##LINE
#define GS_FW_ERR0(LINE) GS_FW_ERR1(LINE)
#define GS_FW_ERR GS_FW_ERR0(__LINE__)
#define GS_FAILWITH(MSG, CAUSE)                                      \
  do {                                                               \
    /* note: re-entrant calls may mean this forms a cycle */         \
    static Err GS_FW_ERR;                                            \
    GS_FW_ERR = GS_ERR_HERE((MSG), (CAUSE));                         \
    return &GS_FW_ERR;                                               \
  } while(0)

#define GS_FAIL_IF(COND, MSG, CAUSE) \
  do {                               \
    if (COND) {                      \
      GS_FAILWITH(MSG, CAUSE);       \
    }                                \
  } while(0)

#define GS_TRY_MSG(CALL, MSG)                                               \
  do {                                                                  \
    Err *gs_try_res = (CALL);                                           \
    GS_FAIL_IF(gs_try_res, MSG, gs_try_res);                       \
  } while(0)

#define GS_TRY(CALL) GS_TRY_MSG(CALL, #CALL)

#define GS_RET_OK return NULL

#define VAL_TAG(VAL) ((VAL) & 3)
#define VAL_IS_TAG(VAL, TAG) (VAL_TAG(VAL) == (TAG))

#define VAL_IS_FIXNUM(VAL) VAL_IS_TAG(VAL, 0)
#define VAL2UFIX(VAL) ((ufix) (VAL) >> 2)
#define VAL2SFIX(VAL) ((ifix) VAL2UFIX(VAL))
#define FIX2VAL(FIX) ((u64) (FIX) << 2)

#define VAL_IS_PTR(VAL) VAL_IS_TAG(VAL, 1)
#define VAL2PTR(TY, VAL) ((TY *) ((uptr) (VAL) & ~1))
#define PTR2VAL(PTR) ((Val) ((uptr) (PTR) | 1))

#define VAL_IS_CONST(VAL) VAL_IS_TAG(VAL, 3)
#define VAL_TRUTHY(VAL) (((VAL) & 7) != 7)
#define VAL_FALSE ((Val) 0x0F) // 0b1111
#define VAL_TRUE ((Val) 0x0B) // 0b1011
#define VAL_NIL ((Val) 0x07) // 0b0111

#define VAL_IS_CHAR(VAL) (VAL_IS_CONST(VAL) && (((VAL) & 0xFFFB) == 0))
#define VAL2CHAR(VAL) ((u32) ((ufix) (VAL) >> 4))
#define CHAR2VAL(VAL) ((((Val) (VAL)) << 4) | 3)

typedef struct Closure Closure;
#define GS_CLOSURE_ARGS Closure *self, u16 argc, Val *args, u16 retc, Val *rets
struct Closure {
  Err *(*call)(GS_CLOSURE_ARGS);
  // extra data
};

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

typedef struct OutStream {
  Closure *write; // (Bytes *slice) -> ()
  Closure *flush; // () -> ()
} OutStream;

#ifdef GS_C_STDIO
#include <stdio.h>
struct GS_FOSClosure {
  Closure cls;
  FILE *file;
};
struct GS_FOSBuf {
  struct GS_FOSClosure write, flush;
};
OutStream gs__file_outstream(FILE *file, struct GS_FOSBuf *buf);
#define GS_FILE_OUTSTREAM(NAME, FILE)                                 \
  struct GS_FOSBuf __gs_fos_buf_##NAME;                               \
  OutStream NAME = gs__file_outstream((FILE), &__gs_fos_buf_##NAME);
#endif

Err *gs_write_error(Err *err, OutStream *stream);

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

#define GS_ALLOC_META(TYPE, COUNT) ((AllocMeta) { sizeof(TYPE), alignof(TYPE), (COUNT) })

static inline anyptr gs_alloc(AllocMeta meta) {
  return gs_current_alloc->table->alloc(gs_current_alloc, meta);
}

static inline anyptr gs_realloc(anyptr ptr, AllocMeta oldMeta, AllocMeta newMeta) {
  return gs_current_alloc->table->realloc(gs_current_alloc, ptr, oldMeta, newMeta);
}

static inline void gs_free(anyptr ptr, AllocMeta meta) {
  return gs_current_alloc->table->free(gs_current_alloc, ptr, meta);
}

typedef struct Symbol {
  Utf8Str name;
  Val value;
} Symbol;

typedef struct SymTableBucket {
  Symbol **syms;
  u32 len;
  u32 cap;
} SymTableBucket;
typedef struct SymTable {
  SymTableBucket *buckets;
  u32 bucketc;
  u32 size;
} SymTable;

u64 gs_hash_bytes(Bytes bytes);
int gs_bytes_cmp(Bytes lhs, Bytes rhs);

SymTable *gs_alloc_sym_table(void);
void gs_free_sym_table(SymTable *table);
Symbol *gs_intern(SymTable *table, Utf8Str name);

#ifdef __cplusplus
  }
}
#endif
