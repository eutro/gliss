#include "rt.h"
#include "bytecode/interp.h"

static Err *gs_main() {
  GS_WITH_ALLOC(&gs_c_alloc) {
    SymTable *table = gs_alloc_sym_table();

    Symbol *how1 = gs_intern(table, GS_UTF8_CSTR("how"));
    Symbol *how2 = gs_intern(table, GS_UTF8_CSTR("how"));
    GS_FAIL_IF(how1 != how2, "Unequal symbols", NULL);

    Symbol *how = how1;

    gs_global_syms = table;
    {
      Insn insns[] = {
        CONST_4, 0xca, 0xfe, 0xba, 0xbe,
        RET, 1,
      };
      InsnSeq insnSeq = { insns, sizeof(insns) / sizeof(Insn), 0, 1 };
      Val ret;
      GS_TRY(gs_interp(&insnSeq, 0, NULL, 1, &ret));
      GS_FAIL_IF(ret != 0xcafebabe, "Bad return", NULL);
    }
    {
      Insn insns0[] = {
        CONST_4, 0xca, 0xfe, 0xba, 0xbe,
        INTERN, 3, 'h', 'o', 'w',
        DYN_1, 3/*sym_deref*/,
        CALL, 1, 1,
        RET, 1,
      };
      InsnSeq insnSeq0 = { insns0, sizeof(insns0) / sizeof(Insn), 0, 2 };
      Insn insns1[] = {
        ARG_REF, 0,
        RET, 1,
      };
      InsnSeq insnSeq1 = { insns1, sizeof(insns1) / sizeof(Insn), 1, 1 };
      InterpClosure closure1 = gs_interp_closure(insnSeq1);
      how->value = PTR2VAL(&closure1);

      Val ret;
      GS_TRY(gs_interp(&insnSeq0, 0, NULL, 1, &ret));
      GS_FAIL_IF(ret != 0xcafebabe, "Bad return", NULL);
    }

    gs_free_sym_table(table);
  }

  GS_RET_OK;
}

int main() {
  Err *err;
  err = gs_main();
  if (err) {
    GS_FILE_OUTSTREAM(gs_stderr, stderr);
    err = gs_write_error(err, &gs_stderr);
    if (err) return 2;
    return 1;
  }
}
