#include "../rt.h"
#include "image.h"
#include "../gc/gc_type.h"

#ifndef GS_STACK_MAX_DEPTH
#  define GS_STACK_MAX_DEPTH 10000
#endif

struct StackFrame {
  Utf8Str name;
  struct StackFrame *next;
};
extern struct ShadowStack {
  u32 depth;
  struct StackFrame *frame;
} gs_shadow_stack;

DEFINE_GC_TYPE(
  InterpClosure,
  NOGC(FIX), Closure, parent,
  GC(FIX, Raw), Image *, img,
  NOGC(FIX), u32, codeRef,
  GC(FIX, Raw), Symbol *, assignedTo,
  NOGC(FIX), u32, capturec,
  GC(RSZ(capturec), Tagged), ValArray, captured
);

Err *gs_interp_closure(Image *img, u32 codeRef, Val *args, u16 argc, InterpClosure **out);

void gs_interp_dump_stack();
