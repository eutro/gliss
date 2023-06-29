#pragma once

#include "../rt.h"

#define SYMBOL_TYPE 0
#define STRING_TYPE 1
#define BYTESTRING_TYPE 2
#define CONS_TYPE 3
#define NATIVE_CLOSURE_TYPE 4
#define INTERP_CLOSURE_TYPE 5

Err *gs_add_primitive_types();
Err *gs_add_primitives(SymTable *table);
