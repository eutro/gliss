#pragma once

#include "../rt.h"
#include "gc_type.h"

typedef u32 TypeIdx;

// allocator

/**
 * Internal state of the garbage collector.
 *
 * Architecture:
 *
 * The garbage collector is a scoped generational garbage collector,
 * with the primary aim of delivering consistent throughput.
 *
 * It is:
 *
 * - Generational, in that objects are sorted into so-called
 *   "generations" of objects, where "younger" generations are
 *   collected more often, with survivors graduating to older
 *   generations.
 *
 * - Scoped, in that the generations mentioned above are created
 *   dynamically to span specific nested "dynamic extents" of code,
 *   with the generation being collected (and survivors graduated)
 *   when the extent is finished.
 *
 * The scoped part exploits that a well-behaved lisp program does not
 * tend to do an awful lot of mutation, which would cause objects to
 * routinely escape their scopes.
 *
 * Scopes can be introduced by the user with the `call-in-new-scope'
 * function.
 *
 *
 *
 * Each scope consists of a number of mini-pages, which house the
 * objects themselves. Objects are placed contiguously in the
 * mini-page (padded with 0xFF for alignment), with the following
 * structure:
 *
 *   |MSB          64 bits            |LSB
 *   |             header             |
 *
 *   |--------------------------------|---------------------
 *   | 8 |         56 bits            |
 *    one      forwarding pointer        fields of object ...
 *
 *     OR
 *
 *   |-------|-------|----------------|---------------------
 *   | 8 | 8 |16 bits|     32 bits    |
 *     |   |   |       |                fields of object ...
 *     |   |   |       |
 *     |   |   |      type of the object, index into types array
 *     |   |   |
 *     |   |  generation, if the object has been written to a field
 *     |   |  of an older generation, the (oldest) generation it has
 *     |   |  been written to, may be indirect (i.e. if a referring
 *     |   |  object is written, this will be updated during marking)
 *     |   |
 *     |  mark, bits for colouring
 *     |
 *    zero, to indicate the end of padding, or two to indicate a large object
 *
 * Mini-page pointers must fit into 56 bits (i.e. the top 8 bits must
 * be 0), so that the first byte of the header can be used to indicate
 * the end of padding, when the header represents a forwarding pointer.
 *
 * It may be the case that a single object is so large that it doesn't
 * fit into a single mini-page, or that fitting it would potentially
 * leave too much space wasted; in this case it is added to the linked
 * list of large objects in the generation (using the same object
 * representation). These too must be checked for the 
 *
 *
 *
 * The following types of garbage collection exist:
 *
 * Scope-end collection:
 *
 * - Occurs when a scope's extent ends. Values that are returned from
 *   the `call-in-new-scope` function are considered as escaping to
 *   the outer scope.
 *
 * - Escaped values are moved to their heap if possible, else a major
 *   collection started.
 *
 * - All other values are garbage, and discarded.
 *
 * In-scope collection:
 *
 * - Occurs when an allocation fails because there are no more free
 *   mini-pages to attach to a scope (i.e. OOM).
 *
 * - Escaped values are moved to their heap if possible, else a major
 *   collection started.
 *
 * - Unescaped, but live, values are compacted within the existing
 *   mini-heaps.
 *
 * - If there is still insufficient space, a major collection is run.
 *
 * Major collection:
 *
 * - User-triggered, or occurs on OOM. Should preferably be run by the
 *   user regularly when there is not a lot going on (such as between
 *   frames), or some lag can be tolerated.
 *
 * - Every root is scanned, determining definitively the set of live
 *   objects.
 *
 * - Older generations are compacted first, since objects from younger
 *   generations may graduate into them.
 *
 * - If this fails to leave enough mini-pages free to service the
 *   cause of the major gc, then it is fatal.
 */
typedef struct GcAllocator GcAllocator;

/** Header tags, the first byte of the header */
enum HeaderTag {
  /** Not actually a header, only padding between objects */
  HtPadding = -1,
  /** A normal object in a mini-page */
  HtNormal = 0,
  /** Rest of the header is a forwarding pointer */
  HtForwarding = 1,
  /** A large object */
  HtLarge = 2,
};

/**
 * Colour tags, the second byte of the header, only used in large
 * objects.
 */
enum ColourTag {
  /** Not visited at all, or children visited already */
  CtUnmarked = 0,
  /** Seen and moved, but children haven't yet been visited */
  CtGray = 1,
};

#define MINI_PAGE_SIZE (2<<14)
typedef struct MiniPage {
  /**
   * Pointers to the next/previous pages in the linked list. If this
   * is owned by a generation, it will be the pages in the generation,
   * if it is unowned then it is the unowned pages.
   */
  struct MiniPage *prev, *next;
  /** Generation this mini-page is in */
  u16 generation;
  /** Bytes of data */
  u16 size;
  /** Start of the data */
  alignas(u64) u8 data[1];
} MiniPage;
#define MINI_PAGE_DATA_SIZE (MINI_PAGE_SIZE - (offsetof(MiniPage, data)))
// upper bound on unused space in a mini-page
#define MINI_PAGE_MAX_OBJECT_SIZE (MINI_PAGE_DATA_SIZE / 8 - sizeof(u64))

/** Just a linked list of objects considered too large to be put into a mini-page directly. */
typedef struct LargeObject {
  struct LargeObject *prev, *next;
  /** The generation this LO belongs to */
  u16 gen;
  alignas(u64) u8 data[1];
} LargeObject;

#define TRAIL_SIZE 31

typedef struct TrailNode TrailNode;
typedef struct Trail {
  struct Trail *younger, *older;
  TrailNode *writes;
  u16 gen;
} Trail;

struct TrailNode {
  TrailNode *next;
  struct TrailWrite {
    /** The actual object that was written */
    anyptr object;
    /**
     * The (tagged) target of the write, where the object was written,
     * the least significant two bits are used to represent the
     * FieldGcTag that the field has.
     */
    anyptr writeTarget;
  } writes[TRAIL_SIZE];
  u8 count;
};

typedef struct Generation {
  /**
   * The newest mini-page owned by this generation. New allocations
   * will be put here.
   *
   * This is the HEAD of the linked list, i.e. its prev is null.
   */
  MiniPage *current;
  /**
   * The first minipage allocated to this generation, the END of the
   * linked list.
   */
  MiniPage *first;
  /**
   * The first gray object in this generation.
   */
  anyptr firstGray;

  /**
   * Linked list of large objects that aren't put in mini-pages.
   *
   * New large objects are PREpended to this list.
   */
  LargeObject *largeObjects;
  /**
   * Pointer to the first non-gray large object in this generation.
   *
   * If this is equal to largeObjects, then there are no gray objects
   * in this generation. Otherwise, all ->prev nodes are gray.
   */
  LargeObject *firstNonGrayLo;

  /**
   * Writes that have been made of objects in this generation to those
   * in older ones; these are the objects that escape.
   */
  Trail *trail;

  /**
   * Number of mini-pages this generation has.
   */
  u32 miniPagec;

  /**
   * The element of the root chain that was at the top when the
   * generation was pushed.
   */
  struct GcRoots *roots;
} Generation;

/** Configuration for the garbage collector */
typedef struct GcConfig {
  u16 scopeCount;
  u32 miniPagec;
} GcConfig;

#define GC_DEFAULT_CONFIG \
  ((GcConfig) {           \
    .scopeCount = 32,     \
    .miniPagec = 1024,    \
  })

/**
 * An element of the root chain.
 */
typedef struct GcRoots GcRoots;
enum GcRootTag {
  GrDirect = 0,
  GrIndirect = 1,
  GrRaw = 2,
  GrSpecial = 3,
};
struct GcRoots {
  /**
   * Pointer to the next element of the chain,
   * bottom 2 bits are for tags.
   */
  GcRoots *next;
};
/** Roots where a single array is referenced directly. */
typedef struct GcRootsDirect {
  GcRoots parent;
  size_t len;
  Val *arr;
} GcRootsDirect;
/** Roots where multiple arrays are referenced directly. */
#define GcRootsIndirect(N) struct {             \
    GcRoots parent;                             \
    size_t len;                                 \
    struct {                                    \
      size_t len;                               \
      Val *arr;                                 \
    } arr[N];                                   \
  }
/** Roots where multiple raw pointer arrays are referenced directly. */
#define GcRootsRaw(N) struct {                  \
    GcRoots parent;                             \
    size_t len;                                 \
    struct {                                    \
      size_t len;                               \
      anyptr *arr;                              \
    } arr[N];                                   \
  }
typedef Err *(*MarkFn)(Val *toMark, anyptr closed);
typedef Err *(*RootFn)(MarkFn mark, anyptr markClosed, anyptr closed);
/** Roots where a callback is used to mark roots. */
typedef struct GcRootsSpecial {
  GcRoots parent;
  RootFn fn;
  anyptr closed;
} GcRootsSpecial;

#define PUSH_GC_ROOTS(Type, name, tag)                                  \
  Type roots_##name;                                                    \
  roots_##name.parent.next = (GcRoots *) ((uptr) gs_global_gc->roots | tag); \
  gs_global_gc->roots = &roots_##name.parent
#define PUSH_DIRECT_GC_ROOTS(size, name, arrIn) \
  PUSH_GC_ROOTS(GcRootsDirect, name, GrDirect); \
  roots_##name.len = (size);                    \
  roots_##name.arr = (arrIn);
#define PUSH_INDIRECT_GC_ROOTS(N, name)                 \
  PUSH_GC_ROOTS(GcRootsIndirect(N), name, GrIndirect);  \
  roots_##name.len = (N)
#define PUSH_RAW_GC_ROOTS(N, name)                  \
  PUSH_GC_ROOTS(GcRootsRaw(N), name, GrIndirect);   \
  roots_##name.len = (N)
#define PUSH_SPECIAL_GC_ROOTS(fn, name, closed)     \
  PUSH_GC_ROOTS(GcRootsSpecial, name, GrSpecial);   \
  roots_##name.fn = (fn);                           \
  roots_##name.closed = (closed)
#define POP_GC_ROOTS(name)                          \
  gs_global_gc->roots = roots_##name.parent.next

#define GS_GC_TRY(CALL) GS_TRY_C(CALL, POP_GC_ROOTS())
#define GS_GC_TRY_MSG(CALL, MSG) GS_TRY_MSG_C(CALL, POP_GC_ROOTS())

struct GcAllocator {
  /** Array of scopes */
  Generation *scopes;
  /** The index in the array of the most specific scope */
  u16 topScope;
  /** Size of the scope array */
  u16 scopeCap;

  /** Pointer to the allocation of mini pages */
  u8 *firstMiniPage;
  /** Number of mini pages that have been allocated */
  u32 miniPagec;
  /**
   * First free mini page, other free pages are in the linked list.
   *
   * This is the END of the linked list, the opposite of the format
   * within generations, so that generation pages can easily be
   * concatenated with this.
   */
  MiniPage *freeMiniPage;
  /** Number of free mini pages */
  u32 freeMiniPagec;

  /** Array of types */
  TypeInfo *types;
  /** Number of types */
  u32 typec;
  /** Size of types array */
  u32 typeCap;

  /** Linked list of registered GC roots. */
  GcRoots *roots;
};

/**
 * The global, publicly available garbage collector used by the gs_gc
 * functions.
 */
extern GcAllocator *gs_global_gc;

/**
 * Create a new garbage collector.
 */
Err *gs_gc_init(GcConfig cfg, GcAllocator *out);

/**
 * Dispose of a garbage collector, freeing all remaining memory
 * managed by it.
 */
Err *gs_gc_dispose(GcAllocator *gc);

/**
 * Add a new type to the garbage collector.
 */
Err *gs_gc_push_type(TypeInfo info, TypeIdx *outIdx);

/**
 * Get the type info of a pointer managed by the garbage collector.
 */
TypeIdx gs_gc_typeinfo(anyptr gcPtr);

/**
 * Force the next allocation to allocate as a LargeObject, which will
 * not move in memory.
 */
void gs_gc_force_next_large();

/**
 * Allocate an object of the given type with the garbage collector.
 */
Err *gs_gc_alloc(TypeIdx ty, anyptr *out);

/**
 * Allocate an array of the given type with the garbage collector.
 * arrayTy must be a special designated array type (gc or byte).
 */
Err *gs_gc_alloc_array(TypeIdx arrayTy, u32 len, anyptr *out);

/**
 * Register a write with the garbage collector.
 */
Err *gs_gc_write_barrier(
  anyptr destinationBase, // base object that <destination> is in
  anyptr destination, // the pointer being written to
  anyptr written, // the value written
  unsigned fieldTag // the FieldGcTag of the field
);

/**
 * Push a new generation onto the garbage collector.
 */
Err *gs_gc_push_scope(void);

/**
 * Release the youngest generation of the garbage collector.
 */
Err *gs_gc_pop_scope(void);

/**
 * Dump debug information about the state of the garbage collector to stderr.
 */
void gs_gc_dump(void);
void gs_gc_dump_object(anyptr obj);
