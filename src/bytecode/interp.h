#include "../rt.h"
#include "ops.h"

extern SymTable *gs_global_syms;

Err *gs_interp(InsnSeq *insns, u16 argc, Val *args, u16 retc, Val *rets);

typedef struct InterpClosure {
  Closure cls;
  InsnSeq insns;
} InterpClosure;

InterpClosure gs_interp_closure(InsnSeq insns);
