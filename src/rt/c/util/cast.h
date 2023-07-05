#pragma once

// unions are allowed to alias their member types, and since C99 it's
// OK to access those members that weren't last written to, this
// should let us read and write through pointers without worrying
// about strict aliasing
#define BIT_CAST(DstTy, SrcTy, val) (((union { DstTy dst; SrcTy src; }) {.src = (val)}).dst)
#define PTR_REF(PointeeTy, val) (((union { PointeeTy dst; u8 src[sizeof(PointeeTy)]; } *)(u8 *)(val))->dst)
