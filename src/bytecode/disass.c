#include "image.h"
#include "le_unaligned.h"
#include "ops.h"

#include <stdio.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

static void dump_constant(Image *img, u32 idx) {
  ConstInfo *ci = img->constants.values[idx];
  u32 ty = get32le(ci->ty);
  eprintf("%u ", ty);
  switch (ty) {
  case CLambda: eprintf("(CLambda)"); break;
  case CList: eprintf("(CList)"); break;
  case CDirect: {
    struct ConstDirect *cd = (anyptr) ci;
    eprintf("(CDirect) 0x%04lu", get64le(cd->value));
    break;
  }
  case CSymbol: {
    eprintf("(CSymbol) <");
    struct ConstBytevec *bi = (anyptr) ci;
    fwrite(bi->data, 1, get32le(bi->len), stderr);
    eprintf(">");
    break;
  }
  case CString: {
    eprintf("(CString) \"");
    struct ConstBytevec *bi = (anyptr) ci;
    fwrite(bi->data, 1, get32le(bi->len), stderr);
    eprintf("\"");
    break;
  }
  default: eprintf("???"); break;
  }
}

void gs_stderr_dump_code(Image *img, CodeInfo *ci) {
  eprintf("   len: %u\n", get32le(ci->len));
  eprintf("   maxStack: %u\n", get32le(ci->maxStack));
  eprintf("   locals: %u\n", get32le(ci->locals));
  eprintf("   code:\n");
  Insn *ip = ci->code;
  Insn *end = ci->code + get32le(ci->len);
  while (ip < end) {
    switch (*ip++) {
    case NOP:
      eprintf("    0x00\tNOP\n");
      break;
    case DROP:
      eprintf("    0x01\tDROP\n");
      break;
    case RET:
      eprintf("    0x02 0x%02x\tRET %u\n", *ip, *ip);
      ip++;
      break;
    case BR:
      eprintf(
        "    0x03 0x%02x 0x%02x 0x%02x 0x%02x\t",
        ip[0], ip[1], ip[2], ip[3]
      );
      eprintf("BR %u\n", read_u32(&ip));
      break;
    case BR_IF_NOT:
      eprintf(
        "    0x04 0x%02x 0x%02x 0x%02x 0x%02x\t",
        ip[0], ip[1], ip[2], ip[3]
      );
      eprintf("BR_IF_NOT %u\n", read_u32(&ip));
      break;
    case LDC:
      eprintf(
        "    0x05 0x%02x 0x%02x 0x%02x 0x%02x\t",
        ip[0], ip[1], ip[2], ip[3]
      );
      u32 idx = read_u32(&ip);
      eprintf("LDC [%u]: ", idx);
      dump_constant(img, idx);
      eprintf("\n");
      break;
    case SYM_DEREF:
      eprintf("    0x06\tSYM_DEREF\n");
      break;
    case LAMBDA:
      eprintf(
        "    0x07 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\t",
        ip[0], ip[1], ip[2], ip[3],
        ip[4], ip[5]
      );
      u32 code = read_u32(&ip);
      u16 arity = read_u16(&ip);
      eprintf("LAMBDA code: %u, arity: %u\n", code, arity);
      break;
    case CALL:
      eprintf(
        "    0x08 0x%02x 0x%02x\tCALL argc: %u, retc: %u\n",
        ip[0], ip[1],
        ip[0], ip[1]
      );
      ip += 2;
      break;
    case LOCAL_REF:
    case LOCAL_SET:
    case ARG_REF:
    case RESTARG_REF:
      eprintf(
        "    0x%02x 0x%02x\t",
        ip[-1], ip[0]
      );
      switch (ip[-1]) {
      case LOCAL_REF: eprintf("LOCAL_REF"); break;
      case LOCAL_SET: eprintf("LOCAL_SET"); break;
      case ARG_REF: eprintf("ARG_REF"); break;
      case RESTARG_REF: eprintf("RESTARG_REF"); break;
      }
      eprintf(" %u\n", ip[0]);
      ip++;
      break;
    default:
      eprintf("    0x%02x\t???\n", ip[-1]);
    }
  }
}

void gs_stderr_dump(Image *img) {
  eprintf(
    "version: %u\n",
    img->version
  );

  eprintf(
    "constants: %u\n",
    img->constants.len
  );
  for (u32 i = 0; i < img->constants.len; ++i) {
    eprintf("  [%u]: ", i);
    dump_constant(img, i);
    eprintf("\n");
  }

  eprintf(
    "codes: %u\n",
    img->codes.len
  );
  for (u32 i = 0; i < img->codes.len; ++i) {
    CodeInfo *ci = img->codes.values[i];
    eprintf("  [%u]:\n", i);
    gs_stderr_dump_code(img, ci);
  }

  if (img->start.code) {
    eprintf(
      "start: %u\n",
      img->start.code - 1
    );
  }
}