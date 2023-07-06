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

#include "gc.h"
#include "gc_macros.h"
#include <stdio.h>
#include "../logging.h"
#include "../bytecode/primitives.h"

#if !defined(GC_DUMP_USE_SIGNALS) && defined(__linux__)
#  define GC_DUMP_USE_SIGNALS 1
#endif
#ifndef GC_DUMP_USE_SIGNALS
#  define GC_DUMP_USE_SIGNALS 0
#endif


#if GC_DUMP_USE_SIGNALS
#include <signal.h>
#include <setjmp.h>
#endif

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

#if GC_DUMP_USE_SIGNALS
static jmp_buf segv_escape;
static sig_atomic_t safe_to_jump = 0;
static anyptr segv_addr;
static struct sigaction old_sigaction;
void gs_gc_uninstall_signals();
static void segv_handler(int signum, siginfo_t *info, void *data) {
  (void) data;
  if (safe_to_jump) {
    segv_addr = info->si_addr;
    longjmp(segv_escape, 1);
  }
  gs_gc_uninstall_signals();
  raise(signum);
}

static bool signals_installed = false;
static bool signals_actually_installed = false;

void gs_gc_install_signals() {
  if (signals_installed) return;

  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_sigaction = segv_handler;
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SA_SIGINFO);

  if (sigaction(SIGSEGV, &act, &old_sigaction) < 0) {
    LOG_WARN("%s", "GC_DUMP_USE_SIGNALS enabled, but sigaction failed");
    signals_actually_installed = false;
  } else {
    signals_actually_installed = true;
  }

  signals_installed = true;
}
void gs_gc_uninstall_signals() {
  signals_installed = false;
  if (signals_actually_installed) {
    sigaction(SIGSEGV, &old_sigaction, NULL);
  }
}
#define gs_gc_begin_if_failed_memory() safe_to_jump++; if (setjmp(segv_escape))
#define gs_gc_end_if_failed_memory() safe_to_jump--;
#define A_I_STR0(X) #X
#define A_I_STR(X) A_I_STR0(X)
#define ACCESS_INVALID(M) eprintf(RED "-- invalid memory access: %p (%s:"A_I_STR(__LINE__)") " M " --\n" NONE, segv_addr, FILENAME);
#else
#define gs_gc_install_signals()
#define gs_gc_uninstall_signals()
#define gs_gc_begin_if_failed_memory() if (false)
#define gs_gc_end_if_failed_memory()
#define ACCESS_INVALID(M)
#endif

static bool dump_object(
  u8 *header,
  TypeInfo **tiO,
  u8 **objO,
  bool isLarge
) {
  TypeIdx ty;
  TypeInfo *ti;
  u8 *obj;
  gs_gc_begin_if_failed_memory() {
    ACCESS_INVALID("- aborting");
    gs_gc_end_if_failed_memory();
    return false;
  } else {
    ty = GC_HEADER_TY(header);
    ti = gs_global_gc->types + ty;
    obj = header + sizeof(u64);
    eprintf(
      "          " CYAN "%p" NONE " (" BLUE "%.*s" NONE "):",
      obj,
      ti->name.len, ti->name.bytes
    );
    if (tiO) *tiO = ti;
    if (objO) *objO = obj;
  }

  gs_gc_begin_if_failed_memory() {
    ACCESS_INVALID("- continuing");
  } else {
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
  }

  gs_gc_begin_if_failed_memory() {
    ACCESS_INVALID("- aborting");
    gs_gc_end_if_failed_memory();
    return false;
  } else {
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
  }
  gs_gc_end_if_failed_memory();
  return true;
}

void gs_gc_dump_object(anyptr obj) {
  gs_gc_install_signals();

  dump_object(GC_PTR_HEADER_REF(obj), NULL, NULL, false);

  gs_gc_uninstall_signals();
}

void gs_gc_dump() {
  gs_gc_install_signals();

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
    eprintf("    trail:\n");

    Trail *trail = iter->trail;
    while (trail && trail->older) trail = trail->older;
    for (; trail; trail = trail->older) {
      eprintf("      gen: %" PRIu16 "\n", trail->gen);
      for (TrailNode *node = trail->writes; node; node = node->next) {
        for (u8 i = 0; i < node->count; ++i) {
          anyptr target = node->writes[i].writeTarget;
          const char *kind;
          switch ((uptr) target & 3) {
          case FieldGcNone: kind = "FieldGcNone"; break;
          case FieldGcTagged: kind = "FieldGcTagged"; break;
          case FieldGcRaw: kind = "FieldGcRaw"; break;
          default: kind = "???"; break;
          }
          target = (anyptr) ((uptr) target & ~3);
          eprintf("        " PURPLE "%p" NONE " (%s) <- " CYAN "%p" NONE "\n", target, kind, node->writes[i].object);
        }
      }
    }

    eprintf("    mini-pages: %" PRIu32 "\n", iter->miniPagec);
    for (MiniPage *mp = iter->first; mp; mp = mp->prev) {
      eprintf("      " PURPLE "%p - %p" NONE ":\n", mp, (u8 *) mp + MINI_PAGE_SIZE);
      if (mp->generation != scope) {
        eprintf("        " RED "wrong generation" NONE "\n");
      }
      eprintf("        used bytes: %" PRIu16 "\n", mp->size);
      eprintf("        objects:\n");

      u8 *end = mp->data + mp->size;
      u8 *header = mp->data;
      while (true) {
        gs_gc_begin_if_failed_memory() {
          ACCESS_INVALID("- aborting");
          break;
        } else {
          while (header < end && *header == (u8) HtPadding) ++header;

          if (header >= end) break;
          TypeInfo *ti;
          u8 *obj;
          if (!dump_object(header, &ti, &obj, false)) {
            break;
          }
          u32 size = ti->layout.size;
          if (ti->layout.resizable.field) {
            Field *rsz = &ti->layout.fields[ti->layout.resizable.field - 1];
            size += PTR_REF(u32, obj + ti->layout.resizable.offset) * rsz->size;
          }
          header = obj + size;
          gs_gc_end_if_failed_memory();
        }
      }
      gs_gc_end_if_failed_memory();
      eprintf("      " PURPLE "large objects" NONE ":\n");
      for (LargeObject *lo = iter->largeObjects; lo; lo = lo->next) {
        dump_object(lo->data, NULL, NULL, true);
      }
    }
  }
  eprintf("----- End Garbage Collector Dump -----\n");

  gs_gc_uninstall_signals();
}
