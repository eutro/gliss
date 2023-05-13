DYN1OP(fx_neg, FIX2VAL(-VAL2SFIX(x)))
DYN1OP(ptr_deref, *VAL2PTR(Val, x))
DYN1OP(sym_ref, PTR2VAL(&VAL2PTR(Symbol, x)->value))
DYN1OP(sym_deref, VAL2PTR(Symbol, x)->value)
