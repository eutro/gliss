#include "rt.h"
#include "gc/gc_type.h"
#include "logging.h"

#include "gc/gc.h"
#include "bytecode/primitives.h"

#include "./bytecode/tck.h"

// for memcmp
#include <string.h>

Err gs_current_err;

Err *gs_call(GS_CLOSURE_ARGS) {
  return self->call(self, argc, args, retc, rets);
}

void gs_write_error(Err *err) {
  fprintf(stderr, BROWN "Uncaught exception" NONE ":");
  if (!(VAL_IS_PTR(err->exn) && VAL2PTR(u8, err->exn) == NULL)) {
    Val exn = err->exn;
    fprintf(stderr, " ");
    pr0(stderr, exn);
  }
  fprintf(stderr, "\n");
  u32 toPrint = err->len < GS_ERR_MAX_TRACE ? err->len : GS_ERR_MAX_TRACE;
  for (u32 i = 0; i < toPrint; ++i) {
    ErrFrame *frame = err->frames + i;
    fprintf(stderr, "  " BROWN "at" NONE " " CYAN "%.*s" NONE " (%.*s:%" PRIu32 ")" ": %.*s\n",
            frame->func.len, frame->func.bytes,
            frame->file.len, frame->file.bytes,
            frame->line,
            frame->msg.len, frame->msg.bytes);
  }
  if (err->len > GS_ERR_MAX_TRACE) {
    u32 omitted = err->len - GS_ERR_MAX_TRACE;
    fprintf(stderr, "  " BROWN "at" NONE " ... (%" PRIu32 " omitted)\n", omitted);
  }
}

#ifdef GS_C_ALLOC
#include <stdlib.h>

static anyptr gs_c_alloc_impl(Allocator *self, AllocMeta meta) {
  (void) self;
  return aligned_alloc(meta.align, (size_t) meta.size * (size_t) meta.count);
}

static anyptr gs_c_realloc_impl(Allocator *self, anyptr ptr, AllocMeta oldMeta, AllocMeta newMeta) {
  (void) self; (void) oldMeta;
  return realloc(ptr, (size_t) newMeta.size * (size_t) newMeta.count);
}

static void gs_c_free_impl(Allocator *self, anyptr ptr, AllocMeta meta) {
  (void) self; (void) meta;
  free(ptr);
}

static AllocatorVt gs_c_alloc_vt = {gs_c_alloc_impl, gs_c_realloc_impl, gs_c_free_impl};
Allocator gs_c_alloc = {&gs_c_alloc_vt};
#endif

Allocator *gs_current_alloc;

u64 gs_hash_bytes(Bytes bytes) {
  u64 result = 0;
  u8
    *it = bytes.bytes,
    *end = it + bytes.len;
  for (; it != end; ++it) {
    result = 31 * result + *it;
  }
  return result;
}

int gs_bytes_cmp(Bytes lhs, Bytes rhs) {
  if (lhs.len != rhs.len) return lhs.len < rhs.len ? -1 : 1;
  return memcmp(lhs.bytes, rhs.bytes, (size_t) lhs.len);
}

SymTable *gs_global_syms;

Err *gs_alloc_sym_table(void) {
  SymTable *table;
  GS_TRY(gs_gc_alloc_array(SYM_TABLE_TYPE, 64, (anyptr *)&table));
  table->size = 0;
  memset(table->buckets, 0, sizeof(SymTableBucket) * table->bucketc);
  gs_global_syms = table;
  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, symbol_invoke_closure) {
  Symbol *through = (Symbol *) ((u8 *) self - offsetof(Symbol, fn));
  Val selfVal = through->value;
  if (selfVal == PTR2VAL_GC(through)) {
    Utf8Str name = GS_DECAY_BYTES(through->name);
    (void) name;
    LOG_ERROR("Undefined symbol called: %.*s", name.len, name.bytes);
    GS_FAILWITH("Called an undefined symbol", NULL);
  } else if (is_callable(selfVal)) {
    Closure *selfF = VAL2PTR(Closure, selfVal);
    return gs_call(selfF, argc, args, retc, rets);
  } else {
    GS_FAILWITH_VAL_MSG("Not a function", selfVal);
  }
}

Err *gs_intern(Utf8Str name, Symbol **out) {
  SymTable *table = gs_global_syms;

  u64 hash = gs_hash_bytes(name);
  SymTableBucket **bucketP = &table->buckets[(u32) hash % table->bucketc];
  SymTableBucket *bucket = *bucketP;

  Symbol **it, **end;
  if (bucket) {
    end = bucket->syms + bucket->len;
    for (it = bucket->syms; it != end && *it != NULL; ++it) {
      if (gs_bytes_cmp(GS_DECAY_BYTES((*it)->name), name) == 0) {
        *out = *it;
        GS_RET_OK;
      }
    }
  }

  // not found, intern new
  Symbol *val;
  GS_TRY(gs_gc_alloc(SYMBOL_TYPE, (anyptr *)&val));
  InlineUtf8Str *iName;
  GS_TRY(gs_gc_alloc_array(STRING_TYPE, name.len, (anyptr *)&iName));
  memcpy(iName->bytes, name.bytes, name.len);
  *val = (Symbol) {
    .fn = symbol_invoke_closure,
    .value = PTR2VAL_GC(val),
    .name = iName,
    .isMacro = false,
  };
  if (!bucket || bucket->len >= bucket->cap) {
    // realloc bucket
    u32 newCap = !bucket ? 1 : bucket->cap * 2;
    SymTableBucket *newBucket;
    GS_TRY(gs_gc_alloc_array(SYM_TABLE_BUCKET_TYPE, newCap, (anyptr *)&newBucket));
    memset(newBucket->syms, 0, newBucket->cap * sizeof(Symbol *));
    if (bucket) {
      memcpy(newBucket->syms, bucket->syms, bucket->cap * sizeof(Symbol *));
    }
    newBucket->len = !bucket ? 0 : bucket->cap;
    gs_gc_write_barrier(gs_global_syms, bucketP, newBucket, FieldGcRaw);
    *bucketP = bucket = newBucket;
  }
  Symbol **target = bucket->syms + bucket->len++;
  GS_TRY(gs_gc_write_barrier(bucket, target, val, FieldGcRaw));
  *target = val;
  table->size++;

  *out = val;
  GS_RET_OK;
}

Symbol *gs_reverse_lookup(Val value) {
  SymTable *table = gs_global_syms;
  for (u32 i = 0; i < table->bucketc; ++i) {
    SymTableBucket *bucket = table->buckets[i];
    if (bucket) {
      for (u32 j = 0; j < bucket->len; ++j) {
        Symbol *sym = bucket->syms[j];
        if (sym->value == value) {
          return sym;
        }
      }
    }
  }
  return NULL;
}
