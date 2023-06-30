#pragma once

static inline bool is_type(Val val, TypeIdx idx) {
  return VAL_IS_GC_PTR(val) && gs_gc_typeinfo(VAL2PTR(u8, val)) == idx;
}

static inline bool is_list0(Val val) {
  return val == VAL_NIL || is_type(val, CONS_TYPE);
}

static inline bool is_symbol0(Val val) {
  return is_type(val, SYMBOL_TYPE);
}

static inline bool is_callable(Val val) {
  TypeIdx ti;
  return VAL_IS_GC_PTR(val) &&
        (ti = gs_gc_typeinfo(VAL2PTR(u8, val)),
         ti == NATIVE_CLOSURE_TYPE ||
         ti == INTERP_CLOSURE_TYPE ||
         ti == SYMBOL_TYPE);
}
