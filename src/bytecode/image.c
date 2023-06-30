#include "image.h"
#include "../rt.h"
#include "interp.h"
#include "../logging.h"
#include "primitives.h"
#include "../gc/gc.h"

#include <stddef.h>
#include <string.h>

#ifndef __BYTE_ORDER__
#  error "unknown byte order"
#else
#  if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
u32 get32le(u32le le) { return le.raw; }
u64 get64le(u64le le) { return le.raw; }
#  else
u32 get32le(u32le le) { return __builtin_bswap32(le.raw); }
u64 get64le(u64le le) { return __builtin_bswap64(le.raw); }
#  endif
#endif

typedef struct ImageReader {
  const u8 *buf;
  u32 len;
  u32 pos;
} ImageReader;

static Err *skipN(ImageReader *rd, size_t count) {
  GS_FAIL_IF(rd->len - rd->pos < count, "not enough bytes in buffer", NULL);
  rd->pos += count;
  GS_RET_OK;
}

static Err *nextN(ImageReader *rd, anyptr out, size_t count) {
  u32 savedPos = rd->pos;
  GS_TRY(skipN(rd, count));
  memcpy(out, rd->buf + savedPos, count);
  GS_RET_OK;
}

#define next_raw(rd, var) nextN(rd, var, sizeof(*var))

static Err *next_u32(ImageReader *rd, u32le *out) {
  return next_raw(rd, out);
}

#define U32_ALIGN 4

static u32 pad_to_align(u32 n) {
  return (n + U32_ALIGN - 1) & -U32_ALIGN;
}

Err *gs_index_image(u32 len, const u8 *buf, Image **retP) {
  LOG_DEBUG("Indexing image at %p", buf);

  Image *ret;
  GS_TRY(gs_gc_alloc(IMAGE_TYPE, (anyptr *)&ret));

  // zero out tables
  memset(ret, 0, sizeof(Image));

  // must be u32 aligned
  GS_FAIL_IF((size_t) buf % U32_ALIGN != 0, "bad alignment of buffer", NULL);

  GS_TRY(gs_gc_alloc_array(BYTESTRING_TYPE, len, (anyptr *)&ret->buf));
  memcpy(ret->buf->bytes, buf, len);
  buf = ret->buf->bytes;
  ImageReader rd = { buf, len, 0 };

  u32le val;
  u32le expectedMagic;
  memcpy(&expectedMagic, "gls\0", 4);
  GS_TRY_MSG(next_u32(&rd, &val), "magic header");
  GS_FAIL_IF(val.raw != expectedMagic.raw, "missing magic header", NULL);

  GS_TRY_MSG(next_u32(&rd, &val), "version");
  ret->version = get32le(val);
  GS_FAIL_IF(ret->version != 1, "unknown version", NULL);

  u32 lastSection = 0;

  u32 minCodeLen = 0;

  while (rd.pos < rd.len) {
    GS_TRY_MSG(next_u32(&rd, &val), "section number");
    u32 section = get32le(val);
    GS_FAIL_IF(section <= lastSection, "out of order sections", NULL);
    lastSection = section;
    switch (section) {
    case SecConstants: {
      GS_TRY_MSG(next_u32(&rd, &val), "constant count");
      u32 constCount = get32le(val);
      GS_TRY(gs_gc_alloc_array(OPAQUE_ARRAY_TYPE, constCount, (anyptr *)&ret->constants));
      ConstInfo **values = ret->constants->values;
      for (u32 constIdx = 0; constIdx < constCount; ++constIdx) {
        ConstInfo *thisPtr = values[constIdx] = (ConstInfo *) (rd.buf + rd.pos);
        GS_TRY_MSG(skipN(&rd, sizeof(u32)), "constant tag");
        switch (get32le(values[constIdx]->ty)) {
        case CLambda: {
          GS_TRY_MSG(skipN(&rd, sizeof(u32)), "lambda code");
          u32 code = get32le(((struct ConstLambda *) thisPtr)->code);
          GS_FAIL_IF(code == UINT32_MAX, "integer overflow", NULL);
          if (code + 1 > minCodeLen) minCodeLen = code + 1;
        }
          // fall through
        case CList: {
          GS_TRY_MSG(next_u32(&rd, &val), "vector length");
          u32 elts = get32le(val);
          GS_FAIL_IF(elts > UINT32_MAX / sizeof(u32), "integer overflow", NULL);
          ConstRef *refs = (ConstRef *) (rd.buf + rd.pos);
          GS_TRY_MSG(skipN(&rd, elts * sizeof(u32)), "vector values");
          for (u32 i = 0; i < elts; ++i) {
            GS_FAIL_IF(get32le(refs[i]) >= constIdx, "constant out of range", NULL);
          }
          break;
        }
        case CString:
        case CSymbol: {
          GS_TRY_MSG(next_u32(&rd, &val), "bytes length");
          u32 len = get32le(val);
          u32 paddedLen = pad_to_align(len);
          GS_FAIL_IF(paddedLen < len, "integer overflow", NULL);
          GS_TRY_MSG(skipN(&rd, paddedLen), "byte data");
          break;
        }
        case CDirect: {
          GS_TRY_MSG(skipN(&rd, sizeof(u64)), "direct value");
          break;
        }
        default: {
          GS_FAILWITH("unrecognised constant type", NULL);
        }
        }
      }
      break;
    }
    case SecCodes: {
      GS_TRY_MSG(next_u32(&rd, &val), "code count");
      u32 len = get32le(val);
      GS_TRY(gs_gc_alloc_array(OPAQUE_ARRAY_TYPE, len, (anyptr *)&ret->codes));
      CodeInfo **values = ret->codes->values;
      for (u32 i = 0; i < len; ++i) {
        values[i] = (CodeInfo *) (rd.buf + rd.pos);
        GS_TRY_MSG(skipN(&rd, sizeof(u32) * 3 /* len, locals, maxStack */), "code header");
        u32 len = get32le(values[i]->len);
        u32 paddedLen = pad_to_align(len);
        GS_FAIL_IF(paddedLen < len, "integer overflow", NULL);
        GS_TRY_MSG(skipN(&rd, paddedLen), "code instructions");
        // TODO validate code?
      }
      break;
    }
    case SecBindings: {
      GS_TRY_MSG(next_u32(&rd, &val), "binding count");
      u32 len = ret->bindings.len = get32le(val);
      ret->bindings.pairs = (BindingInfo *) (rd.buf + rd.pos);
      GS_FAIL_IF(len > UINT32_MAX / sizeof(BindingInfo), "integer overflow", NULL);
      GS_TRY_MSG(skipN(&rd, len * sizeof(BindingInfo)), "binding vector");
      if (len > 0) {
        GS_FAIL_IF(!ret->constants, "no constants", NULL);
        for (BindingInfo *bd = ret->bindings.pairs; bd != ret->bindings.pairs + len; ++bd) {
          GS_FAIL_IF(get32le(bd->symbol) >= ret->constants->len, "symbol constant out of bounds", NULL);
          GS_FAIL_IF(
            get32le(ret->constants->values[get32le(bd->symbol)]->ty) != CSymbol,
            "not a symbol",
            NULL
          );
          GS_FAIL_IF(get32le(bd->binding) >= ret->constants->len, "binding value out of bounds", NULL);
        }
      }
      break;
    }
    case SecStart: {
      GS_TRY(next_u32(&rd, &val));
      u32 codeRef = get32le(val);
      GS_FAIL_IF(!ret->codes || codeRef >= ret->codes->len, "start function out of bounds", NULL);
      GS_FAIL_IF(codeRef == UINT32_MAX, "integer overflow", NULL);
      ret->start.code = codeRef + 1;
      break;
    }
    default:
      GS_FAILWITH("unrecognised section", NULL);
    }
  }

  GS_FAIL_IF(minCodeLen > (!ret->codes ? 0 : ret->codes->len), "code reference out of bounds", NULL);

  *retP = ret;

  GS_RET_OK;
}

static Err *bake_constant(Val *baked_so_far, ConstInfo *info, Val *out) {
  u32 cTy;
  switch (cTy = get32le(info->ty)) {
  case CDirect: {
    struct ConstDirect *cd = (anyptr) info;
    *out = (u64) get32le(cd->lo) | ((u64) get32le(cd->hi) << 32);
    break;
  }
  case CSymbol: {
    struct ConstBytevec *bv = (anyptr) info;
    Symbol *sym;
    GS_TRY(
      gs_intern(
        (Utf8Str) {
          bv->data,
          get32le(bv->len)
        },
        &sym
      )
    );
    *out = PTR2VAL_GC(sym);
    break;
  }
  case CString: {
    struct ConstBytevec *bv = (anyptr) info;
    InlineUtf8Str *theStr;
    GS_TRY(gs_gc_alloc_array(STRING_TYPE, get32le(bv->len), (anyptr *)&theStr));
    memcpy(theStr->bytes, bv->data, theStr->len);
    *out = PTR2VAL_GC(theStr);
    break;
  }
  case CList: {
    struct ConstList *ls = (anyptr) info;
    ConstRef *iter = ls->elements + get32le(ls->len) - 1;
    ConstRef *end = ls->elements - 1;

    Val ret = VAL_NIL;
    for (; iter != end; iter--) {
      Val *pair;
      GS_TRY(gs_gc_alloc(CONS_TYPE, (anyptr *)&pair));
      pair[0] = baked_so_far[get32le(*iter)];
      pair[1] = ret;
      ret = PTR2VAL_GC(pair);
    }
    *out = ret;
    break;
  }
  default: {
    LOG_ERROR("Unknown constant type: %" PRIu32, cTy);
    GS_FAILWITH("Unknown constant type", NULL);
  }
  }

  GS_RET_OK;
}

Err *gs_bake_image(Image *img) {
  if (!img->constantsBaked) {
    GS_TRY(gs_gc_alloc_array(ARRAY_TYPE, img->constants->len, (anyptr *)&img->constantsBaked));
    memset(img->constantsBaked->values, 0, img->constants->len * sizeof(Val));
    for (u32 i = 0; i < img->constants->len; ++i) {
      Val out;
      GS_TRY(
        bake_constant(
          img->constantsBaked->values,
          img->constants->values[i],
          &out
        )
      );
      if (VAL_IS_GC_PTR(out)) {
        gs_gc_write_barrier(img->constantsBaked, &img->constantsBaked->values[i], VAL2PTR(u8, out), FieldGcTagged);
      }
      img->constantsBaked->values[i] = out;
    }
  }

  GS_RET_OK;
}
