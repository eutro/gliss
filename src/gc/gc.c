#include "gc.h"
#include "../logging.h"
#include "gc_macros.h"

#include <string.h> // memset, memcpy
#include <assert.h>

GcAllocator *gs_global_gc;

#define LOG_COLOUR_GC_TRACE LOG_COLOUR_TRACE
#define LOG_COLOUR_GC_DEBUG LOG_COLOUR_DEBUG

#ifndef LOG_ENABLED_GC
#  define LOG_ENABLED_GC 1
#endif

#if LOG_ENABLED_GC
#  define LOG_ENABLED_GC_TRACE LOG_ENABLED_TRACE
#  define LOG_ENABLED_GC_DEBUG LOG_ENABLED_DEBUG
#else
#  define LOG_ENABLED_GC_TRACE false
#  define LOG_ENABLED_GC_DEBUG false
#endif

// insert extra padding between GC objects, to detect heap corruption
#ifndef GC_MIN_PADDING
#  define GC_MIN_PADDING 8
#endif

#define LOG_GC_TRACE(...) LOG(GC_TRACE, __VA_ARGS__)
#define LOG_GC_DEBUG(...) LOG(GC_DEBUG, __VA_ARGS__)

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
    return find_mini_page(header)->generation;
  } else if (*header == HtLarge) {
    return GC_LARGE_OBJECT(header)->gen;
  } else {
    LOG_FATAL("Bad header tag: %" PRIu8, *header);
    return (u16) -1;
  }
}

static Err *gs_fresh_page(u16 genNo, Generation *scope, MiniPage **out) {
  // s-a ok with mini-page headers
  MiniPage *freePage = gs_global_gc->freeMiniPage;
  GS_FAIL_IF(freePage == NULL, "No more pages", NULL);
  MiniPage *prevPage = gs_global_gc->freeMiniPage = freePage->prev;
  gs_global_gc->freeMiniPagec--;
  if (prevPage) prevPage->next = NULL;

  freePage->prev = NULL;
  freePage->next = scope->current;
  if (freePage->next) freePage->next->prev = freePage;
  scope->current = freePage;
  freePage->generation = genNo;
  freePage->size = 0;
  scope->miniPagec++;
  *out = freePage;

  LOG_GC_DEBUG(
    "Attached mini-page to generation %" PRIu16 " (now has %" PRIu32 ", %" PRIu32 " remain free)",
    genNo,
    scope->miniPagec,
    gs_global_gc->freeMiniPagec
  );

  GS_RET_OK;
}

static bool alloc_next_large;
void gs_gc_force_next_large() {
  alloc_next_large = true;
}

static Err *gs_alloc_in_generation(u16 gen, u32 len, TypeIdx tyIdx, anyptr *out, u32 *outSize) {
  Generation *scope = &gs_global_gc->scopes[gen];
  TypeInfo *ty = &gs_global_gc->types[tyIdx];

  LOG_GC_TRACE("Allocating; gen: %" PRIu16 ", len: %" PRIu32 ", ty: %.*s", gen, len, ty->name.len, ty->name.bytes);

  u32 size = ty->layout.size;

  if (ty->layout.resizable.field) {
    size += len * ty->layout.fields[ty->layout.resizable.field - 1].size;
  }
  if (outSize) *outSize = size;

  u32 align = ty->layout.align;

  u8 *hdPtr, *retPtr;
  u8 hdTag;
  if (!alloc_next_large && size <= MINI_PAGE_MAX_OBJECT_SIZE) {
    MiniPage *mPage = scope->current;
  computePadding:;
    u16 padding =
      (align -
       ((offsetof(MiniPage, data) + mPage->size + GC_MIN_PADDING + sizeof(u64))
        % align))
      % align
      + GC_MIN_PADDING;
    u16 position = mPage->size + padding;
    u16 endPosition = position + sizeof(u64) + size;
    if (endPosition > MINI_PAGE_DATA_SIZE) {
      // ran out of space in this page, go again
      GS_TRY(gs_fresh_page(gen, scope, &mPage));
      // TODO minor gc if failed, then major if that fails
      goto computePadding;
    }

    // s-a OK
    memset(mPage->data + mPage->size, -1, padding);
    mPage->size = endPosition;

    hdPtr = mPage->data + position;
    hdTag = HtNormal;
  } else {
    alloc_next_large = false;
    GS_FAIL_IF(align > alignof(u64), "Unsupported large object alignment", NULL);
    LargeObject *lo = gs_alloc(
      GS_ALLOC_ALIGN_SIZE(
        align,
        offsetof(LargeObject, data) + sizeof(u64) + size,
        1
      )
    );
    if (lo == NULL) {
      // TODO major gc, retry
      GS_FAILWITH("OOM, couldn't allocate large object", NULL);
    }
    if ((lo->next = scope->largeObjects)) lo->next->prev = lo;
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

  LOG_GC_TRACE("-> res: %p (%" PRIu16 " bytes used in mini-page)", retPtr, scope->current->size);

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
  Generation *srcScope = &gs_global_gc->scopes[lo->gen];
  if (srcScope->largeObjects == lo) srcScope->largeObjects = lo->next;
  if (srcScope->firstNonGrayLo == lo) srcScope->firstNonGrayLo = lo->next;
  Generation *scope = &gs_global_gc->scopes[targetGen];
  LargeObject *last = scope->largeObjects;
  scope->largeObjects = lo;
  if (lo->next) lo->next->prev = lo->prev;
  if (lo->prev) lo->prev->next = lo->next;
  lo->next = last;
  lo->prev = NULL;
  if (lo->next) lo->next->prev = lo;
  lo->gen = targetGen;
  GC_HEADER_MARK(lo->data) = CtGray;
}

static Err *mark_ptr0(u8 **pointer, u16 dstGen, u16 minMoveGen) {
  u8 *header = GC_PTR_HEADER_REF(*pointer);
  switch (*header) {
  case HtNormal: {
    u16 itsGeneration = find_mini_page(header)->generation;
    if (itsGeneration >= minMoveGen) {
      LOG_GC_TRACE("%s", "Moving to target generation");
      GS_TRY(gs_move_to_generation(dstGen, header, (anyptr *)pointer));
      PTR_REF(u64, header) = FORWARDING_HEADER(*pointer);
    } else {
      LOG_GC_TRACE("%s", "Unmoved");
    }
    break;
  }
  case HtForwarding: {
    LOG_GC_TRACE("%s", "Following forwarding pointer");
    *pointer = READ_FORWARDED(header);
    break;
  }
  case HtLarge: {
    LargeObject *lo = GC_LARGE_OBJECT(*pointer);
    u16 itsGeneration = lo->gen;
    if (itsGeneration > minMoveGen) {
      if (GC_HEADER_MARK(header) == CtUnmarked) {
        LOG_GC_TRACE("%s", "Moving to target generation (large object)");
        gs_move_large_object(lo, dstGen);
        // pointer doesn't move
      } else {
        LOG_GC_TRACE("%s", "Already moved");
      }
    } else {
      LOG_GC_TRACE("%s", "Unmoved (large object)");
    }
    break;
  }
  default: {
    LOG_ERROR("Bad header tag: %" PRIu8, *header);
    GS_FAILWITH("Unrecognised tag", NULL);
  }
  }

  GS_RET_OK;
}

static Err *mark_ptr(u8 **ptr, u16 dstGen, u16 minMoveGen) {
  LOG_GC_TRACE("Marking ptr; ptr: %p, val: %p", ptr, *ptr);
  if (*ptr) {
    GS_TRY(mark_ptr0(ptr, dstGen, minMoveGen));
  } else {
    LOG_GC_TRACE("%s", "Null");
  }
  LOG_GC_TRACE("Marked -> %p", *ptr);
  GS_RET_OK;
}

static Err *mark_val(Val *val, u16 dstGen, u16 minMoveGen) {
  LOG_GC_TRACE("Marking val; ptr: %p, val: 0x%" PRIx64, val, *val);
  if (VAL_IS_GC_PTR(*val)) {
    u8 *pointer = VAL2PTR(u8, *val);
    GS_TRY(mark_ptr0(&pointer, dstGen, minMoveGen));
    *val = PTR2VAL_GC(pointer);
  } else {
    LOG_GC_TRACE("%s", "Constant");
  }
  LOG_GC_TRACE("Marked -> 0x%" PRIx64, *val);
  GS_RET_OK;
}

/**
 * Relocate all references held in obj to dstGen if they are in a
 * generation younger than dstGen, or in dstGen and moveLocal is true.
 */
static Err *gs_visit_gray(anyptr obj, u16 dstGen, bool moveLocal, u8 **objEndOut) {
  TypeIdx tyIdx = GC_HEADER_TY(GC_PTR_HEADER_REF(obj));
  TypeInfo *ti = &gs_global_gc->types[tyIdx];

  LOG_GC_TRACE("Visiting gray; ptr: %p, gen: %" PRIu16 ", ty: %.*s", obj, dstGen, ti->name.len, ti->name.bytes);

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
            GS_TRY(mark_val(&PTR_REF(Val, iter), dstGen, minMoveGen));
          }
        } else {
          end = iter + count * sizeof(u8 *);
          for (; iter != end; iter += sizeof(u8 *)) {
            GS_TRY(mark_ptr(&PTR_REF(u8 *, iter), dstGen, minMoveGen));
          }
        }
      } else {
        if (fieldP->gc == FieldGcTagged) {
          GS_TRY(mark_val(&PTR_REF(Val, head + fieldP->offset), dstGen, minMoveGen));
        } else {
          GS_TRY(mark_ptr(&PTR_REF(u8 *, head + fieldP->offset), dstGen, minMoveGen));
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
  LOG_GC_DEBUG("Marking roots; src: %" PRIu16 ", dst: %" PRIu16, srcGen, dstGen);

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
    case GrRaw: {
      GcRootsRaw(1) *roots = (anyptr) iter;
      for (size_t i = 0; i < roots->len; ++i) {
        anyptr *it = roots->arr[i].arr;
        anyptr *end = it + roots->arr[i].len;
        for (; it != end; ++it) {
          GS_TRY(mark_ptr((u8 **) it, dstGen, srcGen));
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
  LOG_GC_DEBUG("Scanning grays; gen %" PRIu16 ", in-place: %s", gen, inPlace ? "yes" : "no");

  Generation *scope = &gs_global_gc->scopes[gen];
  bool moved;
  do {
    moved = false;
    if (scope->firstNonGrayLo) {
      LargeObject *iter = scope->firstNonGrayLo->prev;
      if (iter) {
        moved = true;
        for (; iter; iter = iter->prev) {
          u8 *header = iter->data;
          GS_TRY(gs_visit_gray(header + sizeof(u64), gen, inPlace, NULL));
          scope->firstNonGrayLo = iter;
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

  for (LargeObject *iter = scope->largeObjects; iter; iter = iter->next) {
    if (GC_HEADER_MARK(iter->data) == CtUnmarked) break;
    GC_HEADER_MARK(iter->data) = CtUnmarked;
  }

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
  LOG_GC_DEBUG("Graduating generation %" PRIu16, gen);

  Generation *scope = &gs_global_gc->scopes[gen];

  Trail *trail = scope->trail;
  while (trail && trail->older) trail = trail->older;
  while (trail) {
    u16 targetGen = trail->gen;
    LOG_GC_DEBUG("Target generation: %" PRIu16, targetGen);
    gs_setup_grays(targetGen);
    for (TrailNode *next, *iter = trail->writes; iter; iter = next) {
      next = iter->next;
      for (u8 i = 0; i < iter->count; ++i) {
        struct TrailWrite *w = iter->writes + i;
        u8 tag = (u8) ((uptr) w->writeTarget & 3);
        u8 *rawTarget = (u8 *) ((uptr) w->writeTarget & ~3);
        u8 *headerRef = GC_PTR_HEADER_REF(w->object);
        LOG_GC_TRACE("Trail entry %p <- %p", rawTarget, w->object);
        if ((tag == FieldGcTagged && PTR_REF(Val, rawTarget) != PTR2VAL_GC(w->object)) ||
            (tag == FieldGcRaw && PTR_REF(anyptr, rawTarget) != w->object)) {
          LOG_GC_TRACE("%s", "Field overwritten, skipped");
          continue;
        }
        anyptr moved;
        switch (*headerRef) {
        case HtNormal: {
          // Hasn't been moved yet, allocate it in the older generation
          GS_TRY(gs_move_to_generation(targetGen, headerRef, &moved));
          PTR_REF(u64, headerRef) = FORWARDING_HEADER(moved);
          LOG_GC_TRACE("%s", "Moved to generation");
          break;
        }
        case HtForwarding: {
          moved = READ_FORWARDED(headerRef);
          LOG_GC_TRACE("%s", "Forwarded");
          break;
        }
        case HtLarge: {
          if (GC_HEADER_MARK(headerRef) == CtUnmarked) {
            LargeObject *lo = GC_LARGE_OBJECT(headerRef);
            gs_move_large_object(lo, targetGen);
            LOG_GC_TRACE("%s", "Large object moved");
          } else {
            LOG_GC_TRACE("%s", "Large object untouched");
          }
          continue;
        }
        default: {
          GS_FAILWITH("Invalid header tag", NULL);
        }
        }
        if (tag == FieldGcTagged) {
          PTR_REF(Val, rawTarget) = PTR2VAL_GC(moved);
        } else if (tag == FieldGcRaw) {
          PTR_REF(anyptr, rawTarget) = moved;
        }
      }
      gs_free(iter, GS_ALLOC_META(TrailNode, 1));
    }
    GS_TRY(gs_scan_grays(targetGen, false));
    Trail *next = trail->younger;
    gs_free(trail, GS_ALLOC_META(Trail, 1));
    trail = next;
  }

  GS_RET_OK;
}

static Err *gs_minor_gc(u16 srcGen, u16 dstGen) {
  LOG_GC_DEBUG("Minor GC; src: %" PRIu16 ", dst: %" PRIu16, srcGen, dstGen);
  LOG_IF_ENABLED(TRACE, {
      fprintf(stderr, RED "Pre-GC dump\n" NONE);
      gs_gc_dump();
    });

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

  LOG_IF_ENABLED(TRACE, {
      fprintf(stderr, RED "Post-GC dump\n" NONE);
      gs_gc_dump();
    });

  GS_RET_OK;
}

Err *gs_gc_push_scope0(u16 gen);
Err *gs_gc_init(GcConfig cfg, GcAllocator *gc) {
  LOG_GC_DEBUG("%s", "Initialising GC");

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
  LOG_GC_TRACE(
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
  LOG_GC_DEBUG("%s", "Disposing of GC");

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

  LOG_GC_DEBUG("Added type %" PRIu32 ": %.*s", pos, info.name.len, info.name.bytes);

  GS_RET_OK;
}

Err *gs_gc_push_scope0(u16 newTop) {
  Generation *top = &gs_global_gc->scopes[newTop];

  top->current = NULL;
  top->largeObjects = NULL;
  top->firstNonGrayLo = NULL;
  top->trail = NULL;
  top->miniPagec = 0;
  top->roots = gs_global_gc->roots;
  GS_TRY(gs_fresh_page(newTop, top, &top->current));
  top->first = top->current;

  GS_RET_OK;
}

Err *gs_gc_push_scope() {
  u16 newTop = ++gs_global_gc->topScope;
  if (newTop >= gs_global_gc->scopeCap) {
    --gs_global_gc->topScope;
    GS_FAILWITH("Too many GC scopes", NULL);
  }

  LOG_GC_DEBUG("Pushing GC scope %" PRIu16, newTop);

  return gs_gc_push_scope0(newTop);
}

Err *gs_gc_pop_scope() {
  u16 oldTop = gs_global_gc->topScope--;

  LOG_GC_DEBUG("Popping GC scope %" PRIu16 " (free mini-pages: %" PRIu32 ")", oldTop, gs_global_gc->freeMiniPagec);

  GS_TRY(gs_minor_gc(oldTop, oldTop - 1));

  Generation *scope = &gs_global_gc->scopes[oldTop];
  MiniPage *mpTail = gs_global_gc->freeMiniPage;
  mpTail->next = scope->current;
  scope->current->prev = mpTail;
  gs_global_gc->freeMiniPage = scope->first;
  gs_global_gc->freeMiniPagec += scope->miniPagec;

  free_all_larges(scope);

  LOG_GC_DEBUG("Popped -> (free mini-pages: %" PRIu32 ")", gs_global_gc->freeMiniPagec);

  GS_RET_OK;
}

static Err *find_trail(Generation *scope, u16 dstGen, TrailNode **out) {
  Trail *younger, *older, *trail = scope->trail;
  younger = older = NULL;
  if (!trail) {
    goto allocateGen;
  } else if (trail->gen == dstGen) {
    // found
  } else if (trail->gen < dstGen) {
    // target is younger than trail
    Trail *last = trail;
    while ((trail = trail->younger) && trail->gen < dstGen) {
      last = trail;
    }
    if (!(trail && trail->gen == dstGen)) {
      // not found
      older = last;
      younger = trail;
      goto allocateGen;
    }
  } else {
    // target is older than trail
    Trail *last = trail;
    while ((trail = trail->older) && trail->gen > dstGen) {
      last = trail;
    }
    if (!(trail && trail->gen == dstGen)) {
      // not found
      younger = last;
      older = trail;
      goto allocateGen;
    }
  }
  if (true) {
    TrailNode *node = trail->writes;
    if (node->count >= TRAIL_SIZE) {
      TrailNode *newNode = trail->writes = gs_alloc(GS_ALLOC_META(TrailNode, 1));
      GS_FAIL_IF(!newNode, "Allocation failed", NULL);
      newNode->next = node;
      newNode->count = 0;
      *out = newNode;
    } else {
      *out = node;
    }
  } else {
  allocateGen:
    trail = gs_alloc(GS_ALLOC_META(Trail, 1));
    GS_FAIL_IF(!trail, "Allocation failed", NULL);
    trail->younger = younger;
    if (younger) younger->older = trail;
    trail->older = older;
    if (older) older->younger = trail;
    trail->gen = dstGen;
    TrailNode *writes = trail->writes = gs_alloc(GS_ALLOC_META(TrailNode, 1));
    GS_FAIL_IF(!writes, "Allocation failed", NULL);
    writes->next = NULL;
    writes->count = 0;
    *out = writes;
  }
  scope->trail = trail;

  GS_RET_OK;
}

Err *gs_gc_write_barrier(
  anyptr destinationBase,
  anyptr destination,
  anyptr written,
  unsigned fieldTag
) {
  u16 dstGen = find_generation(destinationBase);
  u16 srcGen = find_generation(written);
  if (dstGen < srcGen) {
    Generation *scope = &gs_global_gc->scopes[srcGen];
    TrailNode *node;
    GS_TRY(find_trail(scope, dstGen, &node));
    LOG_GC_TRACE("Written to trail; dst: %p, written: %p", destination, written);
    node->writes[node->count++] = (struct TrailWrite) {
      .object = written,
      .writeTarget = (anyptr) ((uptr) destination | fieldTag),
    };
  }
  GS_RET_OK;
}
