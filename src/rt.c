#include "rt.h"

// for memcmp
#include <string.h>

Err *gs_call(GS_CLOSURE_ARGS) {
  return self->call(self, argc, args, retc, rets);
}

#ifdef GS_C_STDIO
static GS_CLOSURE(stdio_write) {
  GS_CHECK_ARITY(1, 0);
  FILE *file = ((struct GS_FOSClosure *) self)->file;
  Bytes *slice = VAL2PTR(Bytes, args[0]);
  fwrite(slice->bytes, 1, slice->len, file);
  GS_RET_OK;
}
static GS_CLOSURE(stdio_flush) {
  FILE *file = ((struct GS_FOSClosure *) self)->file;
  GS_CHECK_ARITY(0, 0);
  fflush(file);
  GS_RET_OK;
}
OutStream gs__file_outstream(FILE *file, struct GS_FOSBuf *buf) {
  *buf = (struct GS_FOSBuf) {
    { { stdio_write }, file },
    { { stdio_flush }, file },
  };
  return (OutStream) {
    (Closure *) &buf->write,
    (Closure *) &buf->flush,
  };
}
#endif

Err *gs_write_error(Err *err, OutStream *stream) {
  Val arg;
  Err *slow = err, *fast = err;
  while (slow) {
    Utf8Str msgHere;

    arg = PTR2VAL(&slow->file);
    GS_TRY(stream->write->call(stream->write, 1, &arg, 0, NULL));

    char buf[64];
    int size = snprintf(buf, sizeof(buf), " (%u): ", slow->line);
    msgHere = (Utf8Str) { (u8 *) buf, (u32) size };
    arg = PTR2VAL(&msgHere);
    GS_TRY(stream->write->call(stream->write, 1, &arg, 0, NULL));

    arg = PTR2VAL(&slow->msg);
    GS_TRY(stream->write->call(stream->write, 1, &arg, 0, NULL));

    msgHere = GS_UTF8_CSTR("\n");
    arg = PTR2VAL(&msgHere);
    GS_TRY(stream->write->call(stream->write, 1, &arg, 0, NULL));

    if (fast) {
      fast = fast->cause;
      if (fast) {
        fast = fast->cause;
      }
    }
    if (slow == fast) {
      msgHere = GS_UTF8_CSTR("  (cycle detected)\n");
      GS_TRY(stream->write->call(stream->write, 1, &arg, 0, NULL));
      break;
    }
    slow = slow->cause;
  }
  GS_TRY(stream->flush->call(stream->flush, 0, NULL, 0, NULL));
  GS_RET_OK;
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

SymTable *gs_alloc_sym_table(void) {
  SymTable *table = gs_alloc(GS_ALLOC_META(SymTable, 1));
  table->bucketc = 64;
  table->size = 0;
  table->buckets = gs_alloc(GS_ALLOC_META(SymTableBucket, table->bucketc));
  memset(table->buckets, 0, sizeof(SymTableBucket) * table->bucketc);
  return table;
}

void gs_free_sym_table(SymTable *table) {
  SymTableBucket
    *it = table->buckets,
    *end = it + table->bucketc;
  for (; it != end; ++it) {
    Symbol
      **sIt = it->syms,
      **sEnd = sIt + it->len;
    for (; sIt != sEnd; ++sIt) {
      gs_free(*sIt, GS_ALLOC_META(Symbol, 1));
    }
    gs_free(it->syms, GS_ALLOC_META(Symbol *, it->cap));
  }
  gs_free(table->buckets, GS_ALLOC_META(SymTableBucket, table->bucketc));
  gs_free(table, GS_ALLOC_META(SymTable, 1));
}

GS_TOP_CLOSURE(STATIC, symbol_trap_closure) {
  (void) self, (void) argc, (void) args, (void) retc, (void) rets;
  GS_FAILWITH("Called an undefined symbol", NULL);
}

Symbol *gs_intern(SymTable *table, Utf8Str name) {
  u64 hash = gs_hash_bytes(name);
  SymTableBucket *bucket = &table->buckets[(u32) hash % table->bucketc];

  Symbol **it, **end = bucket->syms + bucket->len;
  for (it = bucket->syms; it != end && *it != NULL; ++it) {
    if (gs_bytes_cmp((*it)->name, name) == 0) {
      return *it;
    }
  }

  // not found, intern new
  Symbol *val = gs_alloc(GS_ALLOC_META(Symbol, 1));
  *val = (Symbol) {
    name,
    PTR2VAL(&symbol_trap_closure),
    false, // not macro
  };
  if (it == end) {
    // realloc bucket
    u32 newCap = bucket->cap == 0 ? 1 : bucket->cap * 2;
    bucket->syms = gs_realloc(
      bucket->syms,
      GS_ALLOC_META(Symbol *, bucket->cap),
      GS_ALLOC_META(Symbol *, newCap)
    );
    bucket->cap = newCap;
  }
  bucket->syms[bucket->len++] = val;
  table->size++;

  return val;
}

Symbol *gs_reverse_lookup(SymTable *table, Val value) {
  for (u32 i = 0; i < table->bucketc; ++i) {
    SymTableBucket *bucket = table->buckets + i;
    for (u32 j = 0; j < bucket->len; ++j) {
      Symbol *sym = bucket->syms[j];
      if (sym->value == value) {
        return sym;
      }
    }
  }
  return NULL;
}
