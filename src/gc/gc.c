#include "gc.h"

#include <string.h> // memset, memcpy
#include <assert.h>

#include "gc_macros.h"

GcAllocator *gs_global_gc;

static MiniPage *find_mini_page(anyptr ptr) {
  return PTR_CAST(MiniPage, ((((uptr) ptr) / MINI_PAGE_SIZE) * MINI_PAGE_SIZE));
}

TypeInfo *gs_gc_typeinfo(anyptr gcPtr) {
  u32 tyIdx = GC_HEADER_TY(GC_PTR_HEADER_REF(gcPtr));
  return &gs_global_gc->types[tyIdx];
}

static Err *gs_fresh_page(u16 genNo, Generation *scope, MiniPage **out) {
  MiniPage *freePage = gs_global_gc->freeMiniPage;
  GS_FAIL_IF(freePage == NULL, "No more pages", NULL);
  MiniPage *prevPage = gs_global_gc->freeMiniPage = freePage->prev;
  prevPage->next = NULL;

  freePage->prev = NULL;
  freePage->next = scope->current;
  scope->current = freePage;
  freePage->generation = genNo;
  freePage->size = 0;
  scope->miniPagec++;
  *out = freePage;

  GS_RET_OK;
}

static Err *gs_alloc_in_generation(u16 gen, u32 len, TypeIdx tyIdx, anyptr *out, u32 *outSize) {
  Generation *scope = &gs_global_gc->scopes[gen];
  TypeInfo *ty = &gs_global_gc->types[tyIdx];

  u32 size = ty->layout.size;

  if (ty->layout.isArray) {
    size *= len;
    size += sizeof(u64);
  }
  if (outSize) *outSize = size;

  u32 align = ty->layout.align;

  u8 *hdPtr, *retPtr;
  if (size <= MINI_PAGE_MAX_OBJECT_SIZE) {
    MiniPage *mPage = scope->current;
  computePadding:;
    u16 paddingRequired =
      (align -
       ((mPage->size + sizeof(u64))
        % align))
      % align;
    u16 position = mPage->size + paddingRequired;
    u16 endPosition = position + ty->layout.size;
    if (endPosition > MINI_PAGE_DATA_SIZE) {
      // ran out of space in this page, go again
      GS_TRY(gs_fresh_page(gen, scope, &mPage));
      // TODO minor gc if failed, then major if that fails
      goto computePadding;
    }

    memset(mPage->data + mPage->size, -1, paddingRequired);
    mPage->size = endPosition;

    hdPtr = mPage->data + position;
    *hdPtr = HtNormal;
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
    *hdPtr = HtLarge;
  }

  retPtr = hdPtr + sizeof(u64);
  if (ty->layout.isArray) {
    *PTR_CAST(u64, retPtr) = (u64) len;
  }

  { u8 *mrk = &GC_HEADER_MARK(hdPtr);
    *mrk = CtUnmarked; }
  { u16 *gn = &GC_HEADER_GEN(hdPtr);
    *gn = gen; }
  { u32 *t = &GC_HEADER_TY(hdPtr);
    *t = tyIdx; }
  *out = retPtr;

  GS_RET_OK;
}

static Err *gs_move_to_generation(u16 gen, u8 *header, anyptr *out) {
  TypeIdx tyIdx = GC_HEADER_TY(header);
  TypeInfo *ty = &gs_global_gc->types[tyIdx];
  u32 len = 1;
  if (ty->layout.isArray) {
    len = PTR_CAST(u64, header)[1];
  }
  u32 size;
  GS_TRY(gs_alloc_in_generation(gen, len, tyIdx, out, &size));
  memcpy(out, header + sizeof(u64), size);
  GS_RET_OK;
}

Err *gs_gc_alloc(TypeIdx tyIdx, anyptr *out) {
  return gs_alloc_in_generation(gs_global_gc->topScope, 1, tyIdx, out, NULL);
}

Err *gs_gc_alloc_array(TypeIdx tyIdx, u32 len, anyptr *out) {
  return gs_alloc_in_generation(gs_global_gc->topScope, len, tyIdx, out, NULL);
}

#ifndef __BYTE_ORDER__
#  error "unknown byte order"
#else
#  if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#    define READ_FORWARDED(headerRef) ((anyptr) (uptr) (*PTR_CAST(u64, (headerRef)) >> 8))
#    define FORWARDING_HEADER(forwarded) (((u64) HtForwarding << 56) | ((u64) (uptr) (forwarded) >> 8))
#  else
#    define READ_FORWARDED(headerRef) ((anyptr) (uptr) (*PTR_CAST(u64, (headerRef)) & 0x00FFFFFFFFFFFFFF))
#    define FORWARDING_HEADER(forwarded) (((u64) HtForwarding << 56) | (u64) (uptr) (forwarded))
#  endif
#endif

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

/**
 * Relocate all references held in obj to dstGen if they are in a
 * generation younger than dstGen, or in dstGen and moveLocal is true.
 */
static Err *gs_mark(anyptr obj, u16 dstGen, bool moveLocal, u8 **objEndOut) {
  TypeInfo *ti = gs_gc_typeinfo(obj);

  u8 *head = PTR_CAST(u8, obj);
  u64 count;
  if (ti->layout.isArray) {
    count = *PTR_CAST(u64, obj);
    head += sizeof(u64);
  } else {
    count = 1;
  }
  u8 *objEnd = head + ti->layout.size * count;

  for (; head != objEnd; head += ti->layout.size) {
    Val *iter = PTR_CAST(Val, head);
    Val *end = iter + ti->layout.gcFieldc;
    u16 minMoveGen = dstGen + !moveLocal;
    for (; iter != end; ++iter) {
      if (VAL_IS_GC_PTR(*iter)) {
        anyptr pointee = VAL2PTR(u8, *iter);
        u8 *header = GC_PTR_HEADER_REF(pointee);
        switch (*header) {
        case HtNormal: {
          u16 itsGeneration = find_mini_page(header)->generation;
          if (itsGeneration >= minMoveGen) {
            anyptr moved;
            GS_TRY(gs_move_to_generation(dstGen, header, &moved));
            *PTR_CAST(u64, header) = FORWARDING_HEADER(moved);
            *iter = PTR2VAL_GC(moved);
          }
          break;
        }
        case HtForwarding: {
          *iter = PTR2VAL_GC(READ_FORWARDED(header));
          break;
        }
        case HtLarge: {
          LargeObject *lo = GC_LARGE_OBJECT(pointee);
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
      }
    }
  }

  if (objEndOut) *objEndOut = objEnd;
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
          GS_TRY(gs_mark(header + sizeof(u64), gen, inPlace, NULL));
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
            assert(*iter == HtNormal);
            anyptr obj = iter + sizeof(u64);
            GS_TRY(gs_mark(obj, gen, inPlace, &iter));
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

/**
 * Move each object in gen's trail to the older generation it belongs
 * in.
 */
static Err *gs_graduate_generation(u16 gen) {
  Generation *scope = &gs_global_gc->scopes[gen];
  // TODO sort trail
  u16 lastGeneration = 0;
  for (Trail *iter = scope->trail; iter != NULL; iter = iter->next) {
    for (u8 i = 0; i < iter->count; ++i) {
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
        }
        GS_TRY(gs_move_to_generation(thisGeneration, headerRef, &moved));
        *PTR_CAST(u64, headerRef) = FORWARDING_HEADER(moved);
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

  GS_TRY(gs_scan_grays(lastGeneration, false));
  GS_RET_OK;
}

Err *gs_gc_push_scope0(u16 newTop);
Err *gs_gc_init(GcConfig cfg, GcAllocator *gc) {
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

  MiniPage *lastMiniPage = PTR_CAST(MiniPage, gc->firstMiniPage);
  lastMiniPage->prev = NULL;
  for (u32 i = 1; i < gc->miniPagec; ++i) {
    MiniPage *nextMiniPage = PTR_CAST(MiniPage, gc->firstMiniPage + MINI_PAGE_SIZE * i);
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

Err *gs_gc_dispose(GcAllocator *gc) {
  // TODO iterate over large objects

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

  GS_RET_OK;
}

Err *gs_gc_push_scope0(u16 newTop) {
  Generation *top = &gs_global_gc->scopes[newTop];
  top->current = NULL;
  GS_TRY(gs_fresh_page(newTop, top, &top->current));

  top->first = top->current;
  top->largeObjects = NULL;
  top->trail = NULL;

  GS_RET_OK;
}

Err *gs_gc_push_scope() {
  u16 newTop = ++gs_global_gc->topScope;
  if (newTop == gs_global_gc->scopeCap) {
    --gs_global_gc->topScope;
    GS_FAILWITH("Too many GC scopes", NULL);
  }

  return gs_gc_push_scope0(newTop);
}

Err *gs_gc_pop_scope() {
  u16 oldTop = gs_global_gc->topScope--;

  // TODO roots
  GS_TRY(gs_graduate_generation(oldTop));

  Generation *scope = &gs_global_gc->scopes[oldTop];
  MiniPage *mpTail = gs_global_gc->freeMiniPage;
  mpTail->next = scope->current;
  scope->current->prev = mpTail;
  gs_global_gc->freeMiniPage = scope->first;

  GS_RET_OK;
}
