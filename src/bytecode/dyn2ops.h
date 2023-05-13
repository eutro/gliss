DYN2OP(fx_add, x + y)
DYN2OP(fx_sub, x - y)
DYN2OP(fx_mul, FIX2VAL(VAL2UFIX(x) * VAL2UFIX(y)))
DYN2OP(fx_div_s, FIX2VAL(VAL2SFIX(x) / VAL2SFIX(y)))
DYN2OP(fx_div_u, FIX2VAL(VAL2UFIX(x) / VAL2UFIX(y)))

DYN2OP(ptr_store, *VAL2PTR(Val, x) = y)
