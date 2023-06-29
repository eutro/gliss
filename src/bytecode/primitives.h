#pragma once

#include "../rt.h"

#define CONS_TYPE 0
#define INTERP_CLOSURE_TYPE 1

Err *gs_add_primitives(SymTable *table);
