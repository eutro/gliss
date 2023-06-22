#include "rt.h"
#include "bytecode/interp.h"

Err *gs_main() {
  SymTable *table = gs_alloc_sym_table();

  Symbol *how1 = gs_intern(table, GS_UTF8_CSTR("how"));
  Symbol *how2 = gs_intern(table, GS_UTF8_CSTR("how"));
  GS_FAIL_IF(how1 != how2, "Unequal symbols", NULL);

  Symbol *car = gs_intern(table, GS_UTF8_CSTR("car"));
  Symbol *concat = gs_intern(table, GS_UTF8_CSTR("concat"));
  GS_FAIL_IF(car == concat, "Equal symbols", NULL);

  gs_free_sym_table(table);

  GS_RET_OK;
}
