#include "rt.h"
#include "logging.h"

#include "gc/gc.h"
#include "bytecode/primitives.h"

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

    arg = PTR2VAL_NOGC(&slow->file);
    GS_TRY(stream->write->call(stream->write, 1, &arg, 0, NULL));

    char buf[64];
    int size = snprintf(buf, sizeof(buf), " (%u): ", slow->line);
    msgHere = (Utf8Str) { (u8 *) buf, (u32) size };
    arg = PTR2VAL_NOGC(&msgHere);
    GS_TRY(stream->write->call(stream->write, 1, &arg, 0, NULL));

    arg = PTR2VAL_NOGC(&slow->msg);
    GS_TRY(stream->write->call(stream->write, 1, &arg, 0, NULL));

    msgHere = GS_UTF8_CSTR("\n");
    arg = PTR2VAL_NOGC(&msgHere);
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

SymTable *gs_global_syms;

Err *gs_alloc_sym_table(void) {
  SymTable *table;
  GS_TRY(gs_gc_alloc_array(SYM_TABLE_TYPE, 64, (anyptr *)&table));
  table->size = 0;
  memset(table->buckets, 0, sizeof(SymTableBucket) * table->bucketc);
  gs_global_syms = table;
  GS_RET_OK;
}

GS_TOP_CLOSURE(STATIC, symbol_trap_closure) {
  (void) self, (void) argc, (void) args, (void) retc, (void) rets;
  Utf8Str name;
  if (self == &symbol_trap_closure) {
    name = GS_UTF8_CSTR("{direct}");
  } else {
    Symbol *through = (Symbol *) ((u8 *) self - offsetof(Symbol, fn));
    name = GS_DECAY_BYTES(through->name);
  }
  LOG_ERROR("Undefined symbol called: %.*s", name.len, name.bytes);
  GS_FAILWITH("Called an undefined symbol", NULL);
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
    .fn = symbol_trap_closure,
    .value = PTR2VAL_GC(&val->fn),
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
  bucket->syms[bucket->len++] = val;
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
