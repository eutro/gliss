#include "gc.h"
#include "gc_macros.h"
#include <stdio.h>
#include "../logging.h"
#include "../bytecode/primitives.h"

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

static void dump_object(
  u8 *header,
  TypeInfo **tiO,
  u8 **objO,
  bool isLarge
) {
  TypeIdx ty = GC_HEADER_TY(header);
  TypeInfo *ti = gs_global_gc->types + ty;
  u8 *obj = header + sizeof(u64);
  eprintf(
    "          " CYAN "%p" NONE " (" BLUE "%.*s" NONE "):",
    obj,
    ti->name.len, ti->name.bytes
  );

  switch (ty) {
  case STRING_TYPE: {
    InlineUtf8Str *str = &PTR_REF(InlineUtf8Str, obj);
    eprintf(" " YELLOW "\"%.*s\"" NONE "\n", str->len, str->bytes);
    break;
  }
  case SYMBOL_TYPE: {
    Symbol *sym = &PTR_REF(Symbol, obj);
    eprintf(" " PURPLE "%.*s" NONE "\n", sym->name->len, sym->name->bytes);
    break;
  }
  default:
    eprintf("\n");
  }

  Field *iter = ti->layout.fields;
  Field *end = iter + ti->layout.fieldc;
  for (; iter != end; ++iter) {
    eprintf(
      "            " GREEN "%.*s" NONE ": ",
      iter->name.len,
      iter->name.bytes
    );
    u32 count = 1;
    if (ti->layout.resizable.field &&
        iter - ti->layout.fields == ti->layout.resizable.field - 1) {
      if (isLarge) {
        eprintf("...\n");
        continue;
      }
      count = PTR_REF(u32, obj + ti->layout.resizable.offset);
    }
    u8 *fieldPos = obj + iter->offset;
    for (u32 i = 0; i < count;) {
      u8 *start = fieldPos + i * iter->size;
      eprintf("0x");
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      u8 *end = start - 1;
      u8 *fp = end + iter->size;
      i8 d = -1;
#else
      u8 *fp = start;
      u8 *end = fp + iter->size;
      i8 d = 1;
#endif
      for (; fp != end; fp += d) {
        eprintf("%02" PRIx8, *fp);
      }
      if (++i < count) eprintf(", ");
    }
    eprintf("\n");
  }

  if (tiO) *tiO = ti;
  if (objO) *objO = obj;
}

void gs_gc_dump_object(anyptr obj) {
  dump_object(GC_PTR_HEADER_REF(obj), NULL, NULL, false);
}

void gs_gc_dump() {
  eprintf("----- Begin Garbage Collector Dump -----\n");
  eprintf("max scopes: %" PRIu16 ", current: %" PRIu16 "\n", gs_global_gc->scopeCap, gs_global_gc->topScope + 1);
  eprintf("allocation: %p - %p\n", gs_global_gc->firstMiniPage, gs_global_gc->firstMiniPage + gs_global_gc->miniPagec * MINI_PAGE_SIZE);
  eprintf("total mini-pages: %" PRIu32 ", free: %" PRIu32 "\n", gs_global_gc->miniPagec, gs_global_gc->freeMiniPagec);
  eprintf("types: %" PRIu32 "\n", gs_global_gc->typec);
  for (u32 ty = 0; ty < gs_global_gc->typec; ++ty) {
    TypeInfo *ti = gs_global_gc->types + ty;
    eprintf("  [%" PRIu32 "]: " BLUE "%.*s" NONE "\n", ty, ti->name.len, ti->name.bytes);
    eprintf("    size: %" PRIu32 ", align: %" PRIu32 "\n", ti->layout.size, ti->layout.align);
    if (ti->layout.resizable.field) {
      Field *rsz = &ti->layout.fields[ti->layout.resizable.field - 1];
      eprintf("    resizable: " GREEN "%.*s" NONE " * %" PRIu32 "\n", rsz->name.len, rsz->name.bytes, rsz->size);
    }
    eprintf("    %" PRIu16 " fields:\n", ti->layout.fieldc);
    for (u16 field = 0; field < ti->layout.fieldc; ++field) {
      Field *fp = ti->layout.fields + field;
      const char *gcTy;
      switch (fp->gc) {
      case FieldGcNone: gcTy = "None"; break;
      case FieldGcTagged: gcTy = "Tagged"; break;
      case FieldGcRaw: gcTy = "Raw"; break;
      default: gcTy = "???"; break;
      }
      eprintf("      " GREEN "%-16.*s" NONE "\t", fp->name.len, fp->name.bytes);
      eprintf("offset: %" PRIu16 ", size: %" PRIu16 ", gc: %s\n", fp->offset, fp->size, gcTy);
    }
  }
  eprintf("scopes:\n");
  Generation *iter = gs_global_gc->scopes;
  Generation *end = gs_global_gc->scopes + gs_global_gc->topScope + 1;
  for (; iter != end; iter++) {
    u16 scope = iter - gs_global_gc->scopes;
    eprintf("  [%" PRIu16 "]:\n", scope);
    eprintf("    mini-pages: %" PRIu32 "\n", iter->miniPagec);
    for (MiniPage *mp = iter->first; mp; mp = mp->prev) {
      eprintf("      " PURPLE "%p" NONE ":\n", mp);
      if (mp->generation != scope) {
        eprintf("        " RED "wrong generation" NONE "\n");
      }
      eprintf("        used bytes: %" PRIu16 "\n", mp->size);
      eprintf("        objects:\n");

      u8 *end = mp->data + mp->size;
      u8 *header = mp->data;
      while (true) {
        while (header < end && *header == (u8) HtPadding) ++header;
        if (header >= end) break;
        TypeInfo *ti;
        u8 *obj;
        dump_object(header, &ti, &obj, false);
        u32 size = ti->layout.size;
        if (ti->layout.resizable.field) {
          Field *rsz = &ti->layout.fields[ti->layout.resizable.field - 1];
          size += PTR_REF(u32, obj + ti->layout.resizable.offset) * rsz->size;
        }
        header = obj + size;
      }
      eprintf("      " PURPLE "large objects" NONE ":\n");
      for (LargeObject *lo = iter->largeObjects; lo; lo = lo->next) {
        dump_object(lo->data, NULL, NULL, true);
      }
    }
  }
  eprintf("----- End Garbage Collector Dump -----\n");
}
