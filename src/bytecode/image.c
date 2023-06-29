#include "image.h"
#include "../rt.h"
#include "interp.h"
#include "../logging.h"

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

Err *gs_index_image(u32 len, const u8 *buf, Image *ret) {
  LOG_DEBUG("Indexing image at %p", buf);

  // zero out tables
  memset(ret, 0, sizeof(Image));

  // must be u32 aligned
  GS_FAIL_IF((size_t) buf % U32_ALIGN != 0, "bad alignment of buffer", NULL);

  ret->buf = buf;
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
      u32 constCount = ret->constants.len = get32le(val);
      ConstInfo **values
        = ret->constants.values
        = gs_alloc(GS_ALLOC_META(ConstInfo *, constCount));
      GS_FAIL_IF(!values, "could not allocate space for constant table", NULL);
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
      u32 len = ret->codes.len = get32le(val);
      CodeInfo **values
        = ret->codes.values
        = gs_alloc(GS_ALLOC_META(CodeInfo *, len));
      GS_FAIL_IF(!values, "could not allocate space for code table", NULL);
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
      for (BindingInfo *bd = ret->bindings.pairs; bd != ret->bindings.pairs + len; ++bd) {
        GS_FAIL_IF(get32le(bd->symbol) >= ret->constants.len, "symbol constant out of bounds", NULL);
        GS_FAIL_IF(
          get32le(ret->constants.values[get32le(bd->symbol)]->ty) != CSymbol,
          "not a symbol",
          NULL
        );
        GS_FAIL_IF(get32le(bd->binding) >= ret->constants.len, "binding value out of bounds", NULL);
      }
      break;
    }
    case SecStart: {
      GS_TRY(next_u32(&rd, &val));
      u32 codeRef = get32le(val);
      GS_FAIL_IF(codeRef >= ret->codes.len, "start function out of bounds", NULL);
      GS_FAIL_IF(codeRef == UINT32_MAX, "integer overflow", NULL);
      ret->start.code = codeRef + 1;
      break;
    }
    default:
      GS_FAILWITH("unrecognised section", NULL);
    }
  }

  GS_FAIL_IF(minCodeLen > ret->codes.len, "code reference out of bounds", NULL);

  GS_RET_OK;
}

static Err *bake_constant(Val *baked_so_far, ConstInfo *info, Val *out) {
  u32 cTy;
  switch (cTy = get32le(info->ty)) {
  case CDirect: {
    *out = get64le(((struct ConstDirect *) info)->value);
    break;
  }
  case CSymbol: {
    struct ConstBytevec *bv = (anyptr) info;
    *out = PTR2VAL_NOGC(
      gs_intern(
        gs_global_syms,
        (Utf8Str) {
          bv->data,
          get32le(bv->len)
        }
      )
    );
    break;
  }
  case CString: {
    struct ConstBytevec *bv = (anyptr) info;
    Utf8Str *theStr = gs_alloc(GS_ALLOC_META(Utf8Str, 1));
    GS_FAIL_IF(!theStr, "Failed allocation", NULL);
    theStr->len = get32le(bv->len);
    theStr->bytes = bv->data;
    *out = PTR2VAL_NOGC(theStr);
    break;
  }
  case CList: {
    struct ConstList *ls = (anyptr) info;
    ConstRef *iter = ls->elements + get32le(ls->len) - 1;
    ConstRef *end = ls->elements - 1;

    Val ret = VAL_NIL;
    for (; iter != end; iter--) {
      Val *pair = gs_alloc(GS_ALLOC_META(Val, 2));
      GS_FAIL_IF(!pair, "Failed allocation", NULL);
      pair[0] = baked_so_far[get32le(*iter)];
      pair[1] = ret;
      ret = PTR2VAL_NOGC(pair);
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
  if (!img->constants.baked) {
    img->constants.baked =
      gs_alloc(GS_ALLOC_META(Val, img->constants.len));
    for (u32 i = 0; i < img->constants.len; ++i) {
      GS_TRY(
        bake_constant(
          img->constants.baked,
          img->constants.values[i],
          &img->constants.baked[i]
        )
      );
    }
  }

  GS_RET_OK;
}

void gs_free_image(Image *img) {
  gs_free(
    img->constants.values,
    GS_ALLOC_META(ConstInfo *, img->constants.len)
  );
  gs_free(
    img->constants.baked,
    GS_ALLOC_META(Val, img->constants.len)
  );
  gs_free(
    img->codes.values,
    GS_ALLOC_META(CodeInfo *, img->codes.len)
  );
  gs_free(
    img->bindings.pairs,
    GS_ALLOC_META(BindingInfo, img->bindings.len)
  );
  memset(img, 0, sizeof(Image));
}
