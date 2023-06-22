#include "../rt.h"
#include "image.h"

extern SymTable *gs_global_syms;

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

typedef struct InterpClosure {
  Closure cls;
  Image *img;
  u32 codeRef;
  // TODO captures
} InterpClosure;

InterpClosure gs_interp_closure(Image *img, u32 codeRef);
