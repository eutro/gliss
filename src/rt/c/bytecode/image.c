#include "image.h"
#include "../rt.h"
#include "interp.h"
#include "../logging.h"
#include "primitives.h"
#include "../gc/gc.h"
#include "ops.h"
#include "le_unaligned.h"
#include "../util/cast.h"

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

Err *gs_index_image(u32 len, const u8 *buf, Image **retP) {
  LOG_DEBUG("Indexing image at %p", buf);

  Image *ret;
  GS_TRY(gs_gc_alloc(IMAGE_TYPE, (anyptr *)&ret));

  // zero out tables
  memset(ret, 0, sizeof(Image));

  // must be u32 aligned
  GS_FAIL_IF((size_t) buf % U32_ALIGN != 0, "bad alignment of buffer", NULL);

  // ensure the allocated buffer does not move in memory, so internal pointers remain valid
  gs_gc_force_next_large();
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

  Err *err;
  if (false) {
  failed:;
    static char errBuf[32];
    snprintf(errBuf, sizeof(errBuf), "byte index: %" PRIu32, rd.pos);
    GS_FAILWITH(errBuf, err);
  }
#undef GS_FAIL_HERE
#define GS_FAIL_HERE(X) err = (X); goto failed;
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
        CodeInfo *ci = values[i] = (CodeInfo *) (rd.buf + rd.pos);
        GS_TRY_MSG(skipN(&rd, sizeof(u32) * 4 /* len, locals, maxStack, stackMapLen */), "code header");
        u32 len = get32le(values[i]->len);
        u32 paddedLen = pad_to_align(len);
        GS_FAIL_IF(paddedLen < len, "integer overflow", NULL);
        GS_TRY_MSG(skipN(&rd, paddedLen), "code instructions");
        u32 stackMapLen = get32le(ci->stackMapLen);
        GS_FAIL_IF(stackMapLen > UINT32_MAX / 4, "integer overflow", NULL);
        GS_TRY_MSG(skipN(&rd, stackMapLen * 8), "code stack map");
        err = gs_verify_code(ret, ci);
        if (err) {
          LOG_ERROR("Failed verification of code %" PRIu32, i);
          if (LOG_LEVEL >= LVLNO_ERROR) {
            gs_stderr_dump_code(ret, ci);
          }
          GS_FAILWITH("Verification failed", err);
        }
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
#undef GS_FAIL_HERE
#define GS_FAIL_HERE(X) GS_FAIL_HERE_DEFAULT(X)

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

static Err *lookup_label(
  u32 len, StackMapEntry *map,
  u32 key, u32 *stackOut
) {
  StackMapEntry *first = map;
  u32 count = len;

  while (count > 0) {
    u32 step = count / 2;
    StackMapEntry *it = first + step;
    u32 itPos = get32le(it->pos);

    if (itPos == key) {
      *stackOut = get32le(it->height);
      GS_RET_OK;
    } else if (itPos < key) {
      first = ++it;
      count -= step + 1;
    } else {
      count = step;
    }
  }

  GS_FAILWITH("Entry not found", NULL);
}

Err *gs_verify_code(Image *img, CodeInfo *ci) {
  u32 maxStack = get32le(ci->maxStack);
  u32 maxLocals = get32le(ci->locals);

  u32 codeLen = get32le(ci->len);
  Insn *ip = ci->code;
  Insn *start = ip;
  Insn *end = ip + codeLen;

  u32 stackMapLen = get32le(ci->stackMapLen);
  StackMapEntry *stackMap =
    &PTR_REF(StackMapEntry, ip + pad_to_align(codeLen));

  if (stackMapLen > 0) {
    u32 lastIp = get32le(stackMap->pos);
    for (u32 i = 1; i < stackMapLen; i++) {
      u32 thisIp = get32le(stackMap[i].pos);
      // TODO be stricter
      // GS_FAIL_IF(thisIp == lastIp, "Duplicate stack map entry", NULL);
      GS_FAIL_IF(thisIp < lastIp, "Out of order stack map", NULL);
      lastIp = thisIp;
    }
  }

  u32 stackSz = 0;
  bool unreachable = false;

#define POP(N) \
  do {                                                      \
    if (!unreachable) {                                     \
      GS_FAIL_IF(stackSz < (N), "Stack underflow", NULL);   \
      stackSz -= (N);                                       \
    }                                                       \
  } while(0)
#define PUSH(N)                                                 \
  do {                                                          \
    if (!unreachable) {                                         \
      stackSz += (N);                                           \
      GS_FAIL_IF(stackSz > maxStack, "Stack overflow", NULL);   \
    }                                                           \
  } while(0)
#define EXPECT_INSNS(N)                                             \
  do {                                                              \
    GS_FAIL_IF(end - ip < (N), "Unexpected end of code", NULL);     \
  } while(0)

  StackMapEntry *smIter = stackMap;
  StackMapEntry *smEnd = smIter + stackMapLen;
  u32 smIPos = smIter == smEnd ? 0 : get32le(smIter->pos);
  while (ip < end) {
    while (smIter != smEnd && (u32) (ip - start) >= smIPos) {
      GS_FAIL_IF((u32) (ip - start) != smIPos, "Stack map entry is not instruction head", NULL);
      u32 height = get32le(smIter->height);
      GS_FAIL_IF(!unreachable && stackSz != height, "Stack height mismatch", NULL);
      stackSz = height;
      unreachable = false;
      smIter++;
      smIPos = get32le(smIter->pos);
    }
    switch (*ip++) {
    case NOP: break;
    case DROP: {
      POP(1);
      break;
    }
    case BR:
    case BR_IF_NOT: {
      Insn insn = ip[-1];
      EXPECT_INSNS(4);
      i32 off = (i32) read_u32(&ip);
      GS_FAIL_IF(off < start - ip || end - ip <= off, "Jump out of bounds", NULL);
      u32 targetIc = ip - start + off;
      u32 targetStack;
      GS_TRY(lookup_label(stackMapLen, stackMap, targetIc, &targetStack));
      if (insn == BR_IF_NOT) {
        POP(1);
      }
      GS_FAIL_IF(!unreachable && targetStack != stackSz, "Jump target stack height mismatch", NULL);
      if (insn == BR) {
        unreachable = true;
      }
      break;
    }
    case RET: {
      EXPECT_INSNS(1);
      u8 count = *ip++;
      POP(count);
      unreachable = true;
      break;
    }
    case LDC: {
      EXPECT_INSNS(4);
      u32 idx = read_u32(&ip);
      GS_FAIL_IF(!img->constants || idx >= img->constants->len, "Constant out of bounds", NULL);
      PUSH(1);
      break;
    }
    case SYM_DEREF: {
      POP(1);
      PUSH(1);
      break;
    }
    case LAMBDA: {
      EXPECT_INSNS(6);
      u32 idx = read_u32(&ip);
      u16 arity = read_u16(&ip);
      POP(arity);
      GS_FAIL_IF(!img->codes || idx >= img->codes->len, "Code out of range", NULL);
      PUSH(1);
      break;
    }
    case CALL: {
      EXPECT_INSNS(2);
      u8 argc = *ip++;
      u8 retc = *ip++;
      POP((u32) (argc + 1));
      PUSH(retc);
      break;
    }
    case LOCAL_REF:
    case LOCAL_SET: {
      EXPECT_INSNS(1);
      u8 local = *ip++;
      GS_FAIL_IF(local >= maxLocals, "Local out of range", NULL);
      if (ip[-2] == LOCAL_REF) {
        PUSH(1);
      } else {
        POP(1);
      }
      break;
    }
    case ARG_REF:
    case RESTARG_REF: {
      EXPECT_INSNS(1);
      ip++;
      PUSH(1);
      break;
    }
    case THIS_REF: {
      PUSH(1);
      break;
    }
    case CLOSURE_REF: {
      // TODO count closure args statically?
      EXPECT_INSNS(1);
      ip++;
      PUSH(1);
      break;
    }
    default: {
      LOG_ERROR("Unrecognised opcode: 0x%" PRIx8, ip[-1]);
      GS_FAILWITH("Unrecognised opcode", NULL);
    }
    }
  }

  GS_FAIL_IF(!unreachable, "Control may run off the end of function", NULL);
  GS_FAIL_IF(smIter != smEnd, "Stack map has too many entries", NULL);

  GS_RET_OK;
}

Err *gs_bake_image(Image *img) {
  if (!img->constantsBaked) {
    anyptr constantsBaked;
    GS_TRY(gs_gc_alloc_array(ARRAY_TYPE, img->constants->len, &constantsBaked));
    GS_TRY(gs_gc_write_barrier(img, &img->constantsBaked, constantsBaked, FieldGcRaw));
    img->constantsBaked = constantsBaked;
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
        GS_TRY(
          gs_gc_write_barrier(
            img->constantsBaked,
            &img->constantsBaked->values[i],
            VAL2PTR(u8, out),
            FieldGcTagged
          )
        );
      }
      img->constantsBaked->values[i] = out;
    }
  }

  GS_RET_OK;
}
