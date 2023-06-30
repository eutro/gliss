#include "../rt.h"
#include "image.h"
#include "../gc/gc_type.h"

#ifndef GS_STACK_MAX_DEPTH
#  define GS_STACK_MAX_DEPTH 10000
#endif

typedef struct StackFrame {
  struct StackFrame *next;
  enum FrameKind {
    FKInterp,
    FKNative,
  } kind;
} StackFrame;

extern struct ShadowStack {
  u32 depth;
  StackFrame *top;
} gs_shadow_stack;

DEFINE_GC_TYPE(
  InterpClosure,
  NOGC(FIX), Closure, parent,
  GC(FIX, Raw), Image *, img,
  NOGC(FIX), u32, codeRef,
  NOGC(FIX), u32, capturec,
  GC(RSZ(capturec), Tagged), ValArray, captured
);

Err *gs_interp_closure(Image *img, u32 codeRef, Val *args, u16 argc, InterpClosure **out);
