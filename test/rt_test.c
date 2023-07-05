#include "rt.h"
#include "bytecode/interp.h"

Err *gs_main() {
  GS_TRY(gs_alloc_sym_table());

  Symbol *how1, *how2;
  GS_TRY(gs_intern(GS_UTF8_CSTR("how"), &how1));
  GS_TRY(gs_intern(GS_UTF8_CSTR("how"), &how2));
  GS_FAIL_IF(how1 != how2, "Unequal symbols", NULL);

  Symbol *car, *concat;
  GS_TRY(gs_intern(GS_UTF8_CSTR("car"), &car));
  GS_TRY(gs_intern(GS_UTF8_CSTR("concat"), &concat));
  GS_FAIL_IF(car == concat, "Equal symbols", NULL);

  Val c = CHAR2VAL(100);
  GS_FAIL_IF(!VAL_IS_CHAR(c), "Not a char", NULL);
  GS_FAIL_IF(VAL2CHAR(c) != 100, "Wrong char value", NULL);

  GS_RET_OK;
}
