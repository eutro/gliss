#pragma once

#include "../rt.h"

// types

/**
 * A protocol that can be implemented.
 */
typedef struct ProtoKey {
  u64 id;
} ProtoKey;

/**
 * Opaque, structure depends on protocol
 */
typedef struct Vtable Vtable;

/**
 * A table of protocol implementations for a type.
 */
typedef struct ProtoTable {
  /** Array of protocols implemented, sorted ascending by id. */
  ProtoKey **keys;
  /** The respective implementation for each protocol. */
  Vtable **tables;
  /** The size of the table. */
  u32 size;
} ProtoTable;

/**
 * A field of a type.
 */
typedef struct Field {
  /** How many bytes from the object pointer the field is. */
  u32 offset;
  /** How many bytes the field is. */
  u32 size;
} Field;

/**
 * The layout of a type, including everything needed to allocate and
 * scan it.
 */
typedef struct TypeLayout {
  /* needed for GC */
  /**
   * The minimum alignment required by the type.
   * Must be at least 8.
   */
  u32 align;
  /**
   * The number of bytes this type takes in memory.
   */
  u32 size;
  /**
   * The number of fields this type has which should be visible to the
   * GC. These must precede any other fields, with no padding between.
   */
  u32 gcFieldc;

  /**
   * Whether this type represents an array type.
   *
   * If it does, then a u64 size field immediately follows the header
   * (any smaller would just introduce padding), and that many objects
   * described otherwise by this type appear contiguously.
   */
  unsigned isArray : 1;

  /* needed for reflection */
  /**
   * The number of any fields this type has, including both GC-managed
   * and opaque fields.
   */
  u32 fieldc;
  /**
   * Array of fields this type has.
   */
  Field *fields;
} TypeLayout;

/**
 * Information about a type, including protocol implementations,
 * layout, and a human-readable name.
 */
typedef struct TypeInfo {
  /* needed for GC */
  TypeLayout layout;
  /* needed for runtime */
  ProtoTable protos;
  /* for reflection */
  Utf8Str name;
} TypeInfo;

typedef u32 TypeIdx;

// arrays are handled specially by the garbage collector

/** Array of GC-managed values. */
extern TypeIdx gs_gc_array_ty;
/**
 * Array of bytes, with maximal alignment, so they can be interpreted
 * as anything.
 */
extern TypeIdx gs_byte_array_ty;

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

typedef struct Trail {
  struct Trail *next;
  struct TrailWrite {
    /** The actual object that was written */
    anyptr object;
    /** The target of the write, where the object was written */
    Val *writeTarget;
  } writes[TRAIL_SIZE];
  u8 count;
} Trail;

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
    .miniPagec = 32,      \
  })

/**
 * An element of the root chain.
 */
typedef struct GcRoots GcRoots;
enum GcRootTag {
  GrDirect = 0,
  GrIndirect = 1,
  GrSpecial = 2,
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
typedef Err *(*MarkFn)(Val *toMark, anyptr closed);
typedef Err *(*RootFn)(MarkFn mark, anyptr markClosed, anyptr closed);
/** Roots where a callback is used to mark roots. */
typedef struct GcRootsSpecial {
  GcRoots parent;
  RootFn fn;
  anyptr closed;
} GcRootsSpecial;

#define PUSH_GC_ROOTS(Type, tag)                                        \
  Type gliss_gc_root;                                                   \
  gliss_gc_root.parent.next = (GcRoots *) ((uptr) gs_global_gc->roots | tag); \
  gs_global_gc->roots = &gliss_gc_root.parent
#define PUSH_DIRECT_GC_ROOTS(size, arrIn)                  \
  PUSH_GC_ROOTS(GcRootsDirect, GrDirect);                  \
  gliss_gc_root.len = (size);                              \
  gliss_gc_root.arr = (arrIn);
#define PUSH_INDIRECT_GC_ROOTS(N)                          \
  PUSH_GC_ROOTS(GcRootsIndirect(N), GrIndirect);           \
  gliss_gc_root.len = (N)
#define PUSH_SPECIAL_GC_ROOTS(fn, closed)                 \
  PUSH_GC_ROOTS(GcRootsSpecial, GrSpecial);        \
  gliss_gc_root.fn = (fn);                                \
  gliss_gc_root.closed = (closed)
#define POP_GC_ROOTS()                          \
  gs_global_gc->roots = gliss_gc_root.parent.next

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
TypeInfo *gs_gc_typeinfo(anyptr gcPtr);

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
 * Push a new generation onto the garbage collector.
 */
Err *gs_gc_push_scope();

/**
 * Release the youngest generation of the garbage collector.
 */
Err *gs_gc_pop_scope();
