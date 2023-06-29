#include "gc.h"

#include <string.h> // memset, memcpy
#include <assert.h>

#include "../logging.h"

#include "gc_macros.h"

GcAllocator *gs_global_gc;

static MiniPage *find_mini_page(anyptr ptr) {
  // strict-aliasing ok, each mini-page pointer does actually have it
  // as the effective type
  return (MiniPage *) ((((uptr) ptr) / MINI_PAGE_SIZE) * MINI_PAGE_SIZE);
}

TypeIdx gs_gc_typeinfo(anyptr gcPtr) {
  // s-a ok, read through union
  return GC_HEADER_TY(GC_PTR_HEADER_REF(gcPtr));
}

static u16 find_generation(anyptr gcPtr) {
  u8 *header = GC_PTR_HEADER_REF(gcPtr);
  if (*header == HtNormal) {
    return find_mini_page(header)->gen;
  } else {
    return GC_LARGE_OBJECT(header)->gen;
  }
}

static Err *gs_fresh_page(u16 genNo, Generation *scope, MiniPage **out) {
  // s-a ok with mini-page headers
  MiniPage *freePage = gs_global_gc->freeMiniPage;
  GS_FAIL_IF(freePage == NULL, "No more pages", NULL);
  MiniPage *prevPage = gs_global_gc->freeMiniPage = freePage->prev;
  gs_global_gc->freeMiniPagec--;
  prevPage->next = NULL;

  freePage->prev = NULL;
  freePage->next = scope->current;
  scope->current = freePage;
  freePage->generation = genNo;
  freePage->size = 0;
  scope->miniPagec++;
  *out = freePage;

  LOG_DEBUG(
    "Attached mini-page to generation %" PRIu16 " (now has %" PRIu32 ", %" PRIu32 " remain free)",
    genNo,
    scope->miniPagec,
    gs_global_gc->freeMiniPagec
  );

  GS_RET_OK;
}

static Err *gs_alloc_in_generation(u16 gen, u32 len, TypeIdx tyIdx, anyptr *out, u32 *outSize) {
  LOG_TRACE("Allocating; gen: %" PRIu16 ", ty: %" PRIu32 ", len: %" PRIu32, gen, tyIdx, len);

  Generation *scope = &gs_global_gc->scopes[gen];
  TypeInfo *ty = &gs_global_gc->types[tyIdx];

  u32 size = ty->layout.size;

  if (ty->layout.resizable.field) {
    size += len * ty->layout.fields[ty->layout.resizable.field - 1].size;
  }
  if (outSize) *outSize = size;

  u32 align = ty->layout.align;

  u8 *hdPtr, *retPtr;
  u8 hdTag;
  if (size <= MINI_PAGE_MAX_OBJECT_SIZE) {
    MiniPage *mPage = scope->current;
  computePadding:;
    u16 paddingRequired =
      (align -
       ((mPage->size + sizeof(u64))
        % align))
      % align;
    u16 position = mPage->size + paddingRequired;
    u16 endPosition = position + sizeof(u64) + size;
    if (endPosition > MINI_PAGE_DATA_SIZE) {
      // ran out of space in this page, go again
      GS_TRY(gs_fresh_page(gen, scope, &mPage));
      // TODO minor gc if failed, then major if that fails
      goto computePadding;
    }

    // s-a OK
    memset(mPage->data + mPage->size, -1, paddingRequired);
    mPage->size = endPosition;

    hdPtr = mPage->data + position;
    hdTag = HtNormal;
  } else {
    GS_FAIL_IF(align > alignof(u64), "Unsupported large object alignment", NULL);
    LargeObject *lo = gs_alloc(
      GS_ALLOC_ALIGN_SIZE(
        align,
        offsetof(LargeObject, data) + size,
        1
      )
    );
    if (lo == NULL) {
      // TODO major gc, retry
      GS_FAILWITH("OOM, couldn't allocate large object", NULL);
    }
    scope->largeObjects->prev = lo;
    lo->next = scope->largeObjects;
    lo->prev = NULL;
    scope->largeObjects = lo;
    lo->gen = gen;

    hdPtr = lo->data;
    hdTag = HtLarge;
  }

  retPtr = hdPtr + sizeof(u64);
  if (ty->layout.resizable.field) {
    PTR_REF(u32, retPtr + ty->layout.resizable.offset) = len;
  }

  // s-a ok
  PTR_REF(u64, hdPtr) = GC_BUILD_HEADER(hdTag, CtUnmarked, gen, tyIdx);
  *out = retPtr;

  LOG_TRACE("-> res: %p (%" PRIu16 " bytes used in mini-page)", retPtr, scope->current->size);

  GS_RET_OK;
}

static Err *gs_move_to_generation(u16 gen, u8 *header, anyptr *out) {
  TypeIdx tyIdx = GC_HEADER_TY(header);
  TypeInfo *ty = &gs_global_gc->types[tyIdx];
  u32 len = 0;
  if (ty->layout.resizable.field) {
    // s-a ok, through union
    len = (u32) PTR_REF(u32, header + sizeof(u64) + ty->layout.resizable.offset);
  }
  u32 size;
  GS_TRY(gs_alloc_in_generation(gen, len, tyIdx, out, &size));
  memcpy(*out, header + sizeof(u64), size);
  GS_RET_OK;
}

Err *gs_gc_alloc(TypeIdx tyIdx, anyptr *out) {
  return gs_alloc_in_generation(gs_global_gc->topScope, 1, tyIdx, out, NULL);
}

Err *gs_gc_alloc_array(TypeIdx tyIdx, u32 len, anyptr *out) {
  return gs_alloc_in_generation(gs_global_gc->topScope, len, tyIdx, out, NULL);
}

static void gs_move_large_object(LargeObject *lo, u16 targetGen) {
  if (lo->next) lo->next->prev = lo->prev;
  if (lo->prev) lo->prev->next = lo->next;
  Generation *scope = &gs_global_gc->scopes[targetGen];
  LargeObject *last = scope->largeObjects;
  scope->largeObjects = lo;
  lo->next = last;
  lo->prev = NULL;
  lo->gen = targetGen;
  GC_HEADER_MARK(lo->data) = CtGray;
}

static Err *mark_ptr0(u8 **pointer, u16 dstGen, u16 minMoveGen) {
  u8 *header = GC_PTR_HEADER_REF(*pointer);
  switch (*header) {
  case HtNormal: {
    u16 itsGeneration = find_mini_page(header)->generation;
    if (itsGeneration >= minMoveGen) {
      GS_TRY(gs_move_to_generation(dstGen, header, (anyptr *)pointer));
      PTR_REF(u64, header) = FORWARDING_HEADER(*pointer);
    }
    break;
  }
  case HtForwarding: {
    *pointer = READ_FORWARDED(header);
    break;
  }
  case HtLarge: {
    LargeObject *lo = GC_LARGE_OBJECT(*pointer);
    u16 itsGeneration = lo->gen;
    if (itsGeneration > minMoveGen) {
      if (GC_HEADER_MARK(header) == CtUnmarked) {
        gs_move_large_object(lo, dstGen);
        // pointer doesn't move
      }
    }
    break;
  }
  }

  GS_RET_OK;
}

static Err *mark_ptr(u8 **ptr, u16 dstGen, u16 minMoveGen) {
  LOG_TRACE("Marking ptr; ptr: %p, val: %p" PRIx64, ptr, *ptr);
  GS_TRY(mark_ptr0(ptr, dstGen, minMoveGen));
  LOG_TRACE("Marked -> %p", *ptr);
  GS_RET_OK;
}

static Err *mark_val(Val *val, u16 dstGen, u16 minMoveGen) {
  LOG_TRACE("Marking val; ptr: %p, val: 0x%" PRIx64, val, *val);
  if (VAL_IS_GC_PTR(*val)) {
    u8 *pointer = VAL2PTR(u8, *val);
    GS_TRY(mark_ptr0(&pointer, dstGen, minMoveGen));
    *val = PTR2VAL_GC(pointer);
  }
  LOG_TRACE("Marked -> 0x%" PRIx64, *val);
  GS_RET_OK;
}

/**
 * Relocate all references held in obj to dstGen if they are in a
 * generation younger than dstGen, or in dstGen and moveLocal is true.
 */
static Err *gs_visit_gray(anyptr obj, u16 dstGen, bool moveLocal, u8 **objEndOut) {
  LOG_TRACE("Visiting gray; ptr: %p, gen: %" PRIu16, obj, dstGen);

  TypeIdx tyIdx = GC_HEADER_TY(GC_PTR_HEADER_REF(obj));
  TypeInfo *ti = &gs_global_gc->types[tyIdx];

  u8 *head = obj;
  u8 *objEnd = head + ti->layout.size;
  bool resizable = ti->layout.resizable.field != 0;
  u32 count;
  if (resizable) {
    count = PTR_REF(u32, head + ti->layout.resizable.offset);
    objEnd += count * ti->layout.fields[ti->layout.resizable.field - 1].size;
  }

  u16 minMoveGen = dstGen + !moveLocal;
  Field *fields = ti->layout.fields;
  u16 fieldc = ti->layout.fieldc;
  for (u16 field = 0; field < fieldc; ++field) {
    Field *fieldP = fields + field;
    if (fieldP->gc) {
      if (resizable && field == ti->layout.resizable.field - 1) {
        u8 *iter = head + fieldP->offset;
        u8 *end;
        if (fieldP->gc == FieldGcTagged) {
          end = iter + count * sizeof(Val);
          for (; iter != end; iter += sizeof(Val)) {
            mark_val(&PTR_REF(Val, iter), dstGen, minMoveGen);
          }
        } else {
          end = iter + count * sizeof(u8 *);
          for (; iter != end; iter += sizeof(u8 *)) {
            mark_ptr(&PTR_REF(u8 *, iter), dstGen, minMoveGen);
          }
        }
      } else {
        if (fieldP->gc == FieldGcTagged) {
          mark_val(&PTR_REF(Val, head + fieldP->offset), dstGen, minMoveGen);
        } else {
          mark_ptr(&PTR_REF(u8 *, head + fieldP->offset), dstGen, minMoveGen);
        }
      }
    }
  }

  if (objEndOut) *objEndOut = objEnd;
  GS_RET_OK;
}

struct MarkValCls {
  u16 dstGen;
  u16 srcGen;
};
static Err *mark_val_cls(Val *toMark, anyptr closed) {
  struct MarkValCls *cls = closed;
  return mark_val(toMark, cls->dstGen, cls->srcGen);
}
static Err *gs_mark_roots(u16 dstGen, u16 srcGen) {
  LOG_DEBUG("Marking roots; src: %" PRIu16 ", dst: %" PRIu16, srcGen, dstGen);

  Generation *scope = &gs_global_gc->scopes[srcGen];
  for (
    GcRoots *iter = gs_global_gc->roots;
    iter != scope->roots;
    iter = (GcRoots *) ((uptr) iter->next & ~3)
  ) {
    u8 tag = (u8) ((uptr) iter->next & 3);
    switch (tag) {
    case GrDirect: {
      GcRootsDirect *roots = (anyptr) iter;
      Val *end = roots->arr + roots->len;
      for (Val *it = roots->arr; it != end; ++it) {
        GS_TRY(mark_val(it, dstGen, srcGen));
      }
      break;
    }
    case GrIndirect: {
      GcRootsIndirect(1) *roots = (anyptr) iter;
      for (size_t i = 0; i < roots->len; ++i) {
        Val *it = roots->arr[i].arr;
        Val *end = it + roots->arr[i].len;
        for (; it != end; ++it) {
          GS_TRY(mark_val(it, dstGen, srcGen));
        }
      }
      break;
    }
    case GrSpecial: {
      GcRootsSpecial *roots = (anyptr) iter;
      struct MarkValCls cls = { dstGen, srcGen };
      GS_TRY(roots->fn(mark_val_cls, &cls, roots->closed));
      break;
    }
    }
  }

  GS_RET_OK;
}

/**
 * Scan all the gray obects in the given generation, iteratively
 * copying referenced objects to this generation.
 *
 * References to older generations are never copied, references in
 * this generation are copied only if inPlace is true, and references
 * to younger generations are always copied.
 */
static Err *gs_scan_grays(u16 gen, bool inPlace) {
  LOG_DEBUG("Scanning grays; gen %" PRIu16 ", in-place: %s", gen, inPlace ? "yes" : "no");

  Generation *scope = &gs_global_gc->scopes[gen];
  bool moved;
  do {
    moved = false;
    {
      LargeObject *iter = scope->firstNonGrayLo;
      if (iter != scope->largeObjects) {
        moved = true;
        for (
          ;
          iter != scope->largeObjects;
          scope->firstNonGrayLo = iter = iter->prev
        ) {
          u8 *header = iter->data;
          GS_TRY(gs_visit_gray(header + sizeof(u64), gen, inPlace, NULL));
          GC_HEADER_MARK(header) = CtUnmarked;
        }
      }
    }
    {
      u8 *iter = (u8 *) scope->firstGray;
      MiniPage *mp = find_mini_page(iter);
      if (
        mp->prev != NULL ||
        iter < mp->data + mp->size
      ) {
        moved = true;
        while (true) {
          while (iter < mp->data + mp->size) {
            while (*iter == (u8) HtPadding) ++iter;
            if (*iter != (u8) HtNormal) {
              LOG_FATAL("Expected normal tag (0); got: %" PRIu8 ", ptr: %p", *iter, iter);
              GS_FAILWITH("Bad tag", NULL);
            }
            anyptr obj = iter + sizeof(u64);
            GS_TRY(gs_visit_gray(obj, gen, inPlace, &iter));
          }
          mp = mp->prev;
          if (mp == NULL) break;
          iter = mp->data;
        }
        scope->firstGray = iter;
      }
    }
  } while (moved);

  GS_RET_OK;
}

static void gs_setup_grays(u16 gen) {
  Generation *scope = &gs_global_gc->scopes[gen];
  scope->firstGray = scope->current->data + scope->current->size;
  scope->firstNonGrayLo = scope->largeObjects;
}

/**
 * Move each object in gen's trail to the older generation it belongs
 * in.
 */
static Err *gs_graduate_generation(u16 gen) {
  LOG_DEBUG("Graduating generation %" PRIu16, gen);

  Generation *scope = &gs_global_gc->scopes[gen];

  bool emptyTrail = true;
  // TODO sort trail
  u16 lastGeneration = 0;
  for (Trail *iter = scope->trail; iter != NULL; iter = iter->next) {
    for (u8 i = 0; i < iter->count; ++i) {
      emptyTrail = false;
      struct TrailWrite *w = iter->writes + i;
      u8 *headerRef = GC_PTR_HEADER_REF(w->object);
      switch (*headerRef) {
      case HtNormal: {
        // Hasn't been moved yet, allocate it in the older generation
        anyptr moved;
        u16 thisGeneration = GC_HEADER_GEN(headerRef);
        if (thisGeneration > lastGeneration) {
          GS_TRY(gs_scan_grays(lastGeneration, false));
          lastGeneration = thisGeneration;
          gs_setup_grays(thisGeneration);
        }
        GS_TRY(gs_move_to_generation(thisGeneration, headerRef, &moved));
        PTR_REF(u64, headerRef) = FORWARDING_HEADER(moved);
        *w->writeTarget = PTR2VAL_GC(moved);
        break;
      }
        // fall through
      case HtForwarding: {
        anyptr forwarded = READ_FORWARDED(headerRef);
        *w->writeTarget = PTR2VAL_GC(forwarded);
        break;
      }
      case HtLarge: {
        u16 thisGeneration = GC_HEADER_GEN(headerRef);
        if (thisGeneration > lastGeneration) {
          GS_TRY(gs_scan_grays(lastGeneration, false));
          lastGeneration = thisGeneration;
          gs_setup_grays(thisGeneration);
        }
        if (GC_HEADER_MARK(headerRef) == CtUnmarked) {
          LargeObject *lo = GC_LARGE_OBJECT(headerRef);
          u16 targetGen = GC_HEADER_GEN(headerRef);
          gs_move_large_object(lo, targetGen);
        }
        break;
      }
      default: {
        GS_FAILWITH("Invalid header tag", NULL);
      }
      }
    }
  }

  if (!emptyTrail) {
    GS_TRY(gs_scan_grays(lastGeneration, false));
  }
  GS_RET_OK;
}

static Err *gs_minor_gc(u16 srcGen, u16 dstGen) {
  LOG_DEBUG("Minor GC; src: %" PRIu16 ", dst: %" PRIu16, srcGen, dstGen);

  GS_TRY(gs_graduate_generation(srcGen));
  bool inPlace = dstGen == srcGen;
  Generation *scope;
  MiniPage *oldHd, *oldTl, *newTl;
  u32 mpCount;

  if (inPlace) {
    scope = &gs_global_gc->scopes[srcGen];
    oldTl = scope->first;
    oldHd = scope->current;
    mpCount = scope->miniPagec;
    scope->miniPagec = 0;
    scope->first = scope->current = NULL;
    GS_TRY(gs_fresh_page(srcGen, scope, &newTl));
    scope->first = newTl;
  }

  gs_setup_grays(dstGen);
  GS_TRY(gs_mark_roots(dstGen, srcGen));
  GS_TRY(gs_scan_grays(dstGen, inPlace));

  if (inPlace) {
    MiniPage *mpTail = gs_global_gc->freeMiniPage;
    mpTail->next = oldHd;
    oldHd->prev = mpTail;
    gs_global_gc->freeMiniPage = oldTl;
    gs_global_gc->freeMiniPagec += mpCount;
  }

  GS_RET_OK;
}

Err *gs_gc_push_scope0(u16 gen);
Err *gs_gc_init(GcConfig cfg, GcAllocator *gc) {
  LOG_DEBUG("%s", "Initialising GC");

  gc->scopes = gs_alloc(GS_ALLOC_META(Generation, cfg.scopeCount));
  gc->topScope = 0;
  gc->scopeCap = cfg.scopeCount;

  AllocMeta miniPages = GS_ALLOC_ALIGN_SIZE(
    MINI_PAGE_SIZE,
    MINI_PAGE_SIZE,
    cfg.miniPagec
  );
  gc->firstMiniPage = gs_alloc(miniPages);
  gc->freeMiniPagec = gc->miniPagec = cfg.miniPagec;

  gc->typeCap = 32;
  gc->types = gs_alloc(GS_ALLOC_META(TypeInfo, gc->typeCap));
  gc->typec = 0;

  gc->roots = NULL;

  if(
    !gc->scopes ||
    !gc->firstMiniPage ||
    !gc->types
  ) {
    gs_free(gc->scopes, GS_ALLOC_META(Generation, cfg.scopeCount));
    gs_free(gc->firstMiniPage, miniPages);
    gs_free(gc->types, GS_ALLOC_META(TypeInfo, gc->typeCap));
    GS_FAILWITH("Failed allocation", NULL);
  }
  LOG_TRACE(
    "GC space: mini-pages: %p - %p",
    gc->firstMiniPage,
    (u8 (*)[MINI_PAGE_SIZE]) gc->firstMiniPage + gc->miniPagec
  );

  MiniPage *lastMiniPage = &PTR_REF(MiniPage, gc->firstMiniPage);
  lastMiniPage->prev = NULL;
  for (u32 i = 1; i < gc->miniPagec; ++i) {
    MiniPage *nextMiniPage = &PTR_REF(MiniPage, gc->firstMiniPage + MINI_PAGE_SIZE * i);
    lastMiniPage->next = nextMiniPage;
    nextMiniPage->prev = lastMiniPage;
    lastMiniPage = nextMiniPage;
  }
  lastMiniPage->next = NULL;
  gc->freeMiniPage = lastMiniPage;

  gs_global_gc = gc;
  gs_gc_push_scope0(0);

  GS_RET_OK;
}

void free_all_larges(Generation *scope) {
  for (LargeObject *lo = scope->largeObjects; lo != NULL;) {
    LargeObject *nxt = lo->next;
    u8 *header = lo->data;
    TypeIdx ty = GC_HEADER_TY(header);
    TypeInfo *ti = &gs_global_gc->types[ty];
    u32 size = ti->layout.size;
    if (ti->layout.resizable.field) {
      size +=
        ti->layout.fields[ti->layout.resizable.field - 1].size *
        PTR_REF(u32, header + sizeof(u64) + ti->layout.resizable.offset);
    }
    gs_free(lo, GS_ALLOC_ALIGN_SIZE(ti->layout.align, size, 1));
    lo = nxt;
  }
}

Err *gs_gc_dispose(GcAllocator *gc) {
  LOG_DEBUG("%s", "Disposing of GC");

  Generation *end = gc->scopes + gc->topScope + 1;
  for (Generation *gen = gc->scopes; gen != end; ++gen) {
    free_all_larges(gen);
  }

  gs_free(gc->scopes, GS_ALLOC_META(Generation, gc->scopeCap));
  gs_free(gc->firstMiniPage, GS_ALLOC_ALIGN_SIZE(MINI_PAGE_SIZE, MINI_PAGE_SIZE, gc->miniPagec));
  gs_free(gc->types, GS_ALLOC_META(TypeInfo, gc->typeCap));

  gs_global_gc = NULL;
  GS_RET_OK;
}

Err *gs_gc_push_type(TypeInfo info, TypeIdx *outIdx) {
  u32 pos = gs_global_gc->typec++;
  if (pos >= gs_global_gc->typeCap) {
    gs_global_gc->types = gs_realloc(
      gs_global_gc->types,
      GS_ALLOC_META(TypeInfo, gs_global_gc->typec),
      GS_ALLOC_META(TypeInfo, gs_global_gc->typec *= 2)
    );
    GS_FAIL_IF(gs_global_gc->types == NULL, "Could not resize", NULL);
  }
  gs_global_gc->types[pos] = info;
  *outIdx = pos;

  LOG_DEBUG("Added type %" PRIu32, pos);

  GS_RET_OK;
}

Err *gs_gc_push_scope0(u16 newTop) {
  Generation *top = &gs_global_gc->scopes[newTop];

  top->current = NULL;
  top->largeObjects = NULL;
  top->trail = NULL;
  top->miniPagec = 0;
  top->roots = gs_global_gc->roots;
  GS_TRY(gs_fresh_page(newTop, top, &top->current));
  top->first = top->current;

  GS_RET_OK;
}

Err *gs_gc_push_scope() {
  u16 newTop = ++gs_global_gc->topScope;
  if (newTop == gs_global_gc->scopeCap) {
    --gs_global_gc->topScope;
    GS_FAILWITH("Too many GC scopes", NULL);
  }

  LOG_DEBUG("Pushing GC scope %" PRIu16, newTop);

  return gs_gc_push_scope0(newTop);
}

Err *gs_gc_pop_scope() {
  u16 oldTop = gs_global_gc->topScope--;

  LOG_DEBUG("Popping GC scope %" PRIu16 " (free mini-pages: %" PRIu32 ")", oldTop, gs_global_gc->freeMiniPagec);

  GS_TRY(gs_minor_gc(oldTop, oldTop - 1));

  Generation *scope = &gs_global_gc->scopes[oldTop];
  MiniPage *mpTail = gs_global_gc->freeMiniPage;
  mpTail->next = scope->current;
  scope->current->prev = mpTail;
  gs_global_gc->freeMiniPage = scope->first;
  gs_global_gc->freeMiniPagec += scope->miniPagec;

  free_all_larges(scope);

  LOG_DEBUG("Popped -> (free mini-pages: %" PRIu32 ")", gs_global_gc->freeMiniPagec);

  GS_RET_OK;
}

Err *gs_gc_write_barrier(anyptr writtenTo, anyptr ptrWritten) {
  u16 dstGen = find_generation(writtenTo);
  u16 srcGen = find_generation(ptrWritten);
  if (dstGen < srcGen) {
    // TODO record in trail
    u8 *srcHd = GC_PTR_HEADER_REF(ptrWritten);
  }
  GS_RET_OK;
}
