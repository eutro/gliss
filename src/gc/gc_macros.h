#pragma once

#define GC_PTR_HEADER_REF(ptr) ((u8 *)(ptr) - sizeof(u64))
#define GC_HEADER_MARK(header) (PTR_CAST(u8, header)[1])
#define GC_HEADER_GEN(header) (PTR_CAST(u16, header)[1])
#define GC_HEADER_TY(header) (PTR_CAST(u32, header)[1])
