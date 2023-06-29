#pragma once

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
  u16 offset;
  /** How many bytes the field is. */
  u16 size;
  /** Whether the value in the field is visible to the garbage collector. */
  unsigned gc : 1;
} Field;

/**
 * The layout of a type, including everything needed to allocate and
 * scan it.
 */
typedef struct TypeLayout {
  /* needed for GC */
  /**
   * The minimum alignment required by the entire type.
   *
   * Must be at least 8.
   */
  u32 align;

  /**
   * The number of bytes this type takes in memory, including
   * any padding, but excluding resizable fields.
   *
   * If the type is fixed size, then this is the exact size
   * of the type; otherwise, the actual size of the type
   * is this, plus the extra space allocated for the resizable field.
   */
  u32 size;

  /**
   * The offset and index of the resizable field, if any.
   */
  struct {
    /**
     * Zero if the type is fixed-size, otherwise 1 + the index of the
     * field which is resizable.
     */
    u16 field;
    /**
     * Byte offset of the u32 field that holds the length.
     */
    u16 offset;
  } resizable;

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

#define BAD_ARITY "this should not be called"
#define GC_TYPE_EXPAND(X) X
#define GC_TYPE_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, NAME, ...) NAME
#define GC_TYPE_PASTE(Fst, ...)                 \
  GC_TYPE_EXPAND(                               \
    GC_TYPE_GET_MACRO(                          \
      __VA_ARGS__,                              \
      BAD_ARITY, BAD_ARITY, GC_TYPE_PASTE15,    \
      BAD_ARITY, BAD_ARITY, GC_TYPE_PASTE12,    \
      BAD_ARITY, BAD_ARITY, GC_TYPE_PASTE9,     \
      BAD_ARITY, BAD_ARITY, GC_TYPE_PASTE6,     \
      BAD_ARITY, BAD_ARITY, GC_TYPE_PASTE3,     \
    )(Fst, __VA_ARGS__))
#define GC_TYPE_PASTE3(Fst, f, gc1, ty1, nm1) f(Fst, gc1, ty1, nm1)
#define GC_TYPE_PASTE6(Fst, f, gc1, ty1, nm1, ...) f(Fst, gc1, ty1, nm1) GC_TYPE_PASTE3(Fst, f, __VA_ARGS__)
#define GC_TYPE_PASTE9(Fst, f, gc1, ty1, nm1, ...) f(Fst, gc1, ty1, nm1) GC_TYPE_PASTE6(Fst, f, __VA_ARGS__)
#define GC_TYPE_PASTE12(Fst, f, gc1, ty1, nm1, ...) f(Fst, gc1, ty1, nm1) GC_TYPE_PASTE9(Fst, f, __VA_ARGS__)
#define GC_TYPE_PASTE15(Fst, f, gc1, ty1, nm1, ...) f(Fst, gc1, ty1, nm1) GC_TYPE_PASTE12(Fst, f, __VA_ARGS__)

// for struct fields
#define GC_TYPE_STRUCT_FIELD(_i, _gc, ty, nm) ty nm;

// for Field objects
#define GC_TYPE_FIELD_IS_GC_GC(...) 1
#define GC_TYPE_FIELD_IS_GC_NOGC(...) 0
#define GC_TYPE_FIELD_META(Name, GC, ty, nm)                            \
  { .offset = offsetof(Name, nm), .size = sizeof(ty), .gc = GC_TYPE_FIELD_IS_GC_##GC },

// for finding resizable field
#define GC_GET_FIX_GC(ARG) GC_GET_FIX_##ARG
#define GC_GET_FIX_NOGC(ARG) GC_GET_FIX_##ARG
#define GC_GET_FIX_FIX FIX
#define GC_GET_FIX_RSZ(ARG) RSZ
#define GC_GET_FIX(GC) GC_GET_FIX_##GC

// for getting the resizable field index
#define GC_COUNT_RSZ1(FIX) GC_COUNT_RSZ_##FIX
#define GC_COUNT_RSZ0(FIX) GC_COUNT_RSZ1(FIX)
#define GC_COUNT_RSZ(_i, gc, _ty, _nm) GC_COUNT_RSZ0(GC_GET_FIX(gc))
#define GC_COUNT_RSZ_FIX 1 +
#define GC_COUNT_RSZ_RSZ 1) + (

// for getting the resizable length offset
#define GC_RSZ_ARG_RSZ(ARG) ARG
#define GC_RSZ_ARG_NOGC(ARG) GC_RSZ_ARG_##ARG
#define GC_RSZ_ARG_GC(ARG) GC_RSZ_ARG_##ARG
#define GC_RSZ_OFFSET_RSZ(Name, gc) offsetof(Name, GC_RSZ_ARG_##gc) +
#define GC_RSZ_OFFSET_FIX(_Name, _gc) 0 +
#define GC_RSZ_OFFSET1(Name, gc, FIX) GC_RSZ_OFFSET_##FIX(Name, gc)
#define GC_RSZ_OFFSET0(Name, gc, FIX) GC_RSZ_OFFSET1(Name, gc, FIX)
#define GC_RSZ_OFFSET(Name, gc, _ty, _nm) GC_RSZ_OFFSET0(Name, gc, GC_GET_FIX(gc))

// for overriding sizeof-based size with offsetof-based size if there
// is a resizable field
#define GC_RSZ_FIELD_BASED_SIZE_RSZ(Name, nm) * 0 + offsetof(Name, nm)
#define GC_RSZ_FIELD_BASED_SIZE_FIX(Name, nm)
#define GC_RSZ_FIELD_BASED_SIZE1(Name, nm, FIX) GC_RSZ_FIELD_BASED_SIZE_##FIX(Name, nm)
#define GC_RSZ_FIELD_BASED_SIZE0(Name, nm, FIX) GC_RSZ_FIELD_BASED_SIZE1(Name, nm, FIX)
#define GC_RSZ_FIELD_BASED_SIZE(Name, gc, _ty, nm) GC_RSZ_FIELD_BASED_SIZE0(Name, nm, GC_GET_FIX(gc))

/**
 * Usage:
 *
 * DEFINE_GC_TYPE(
 *   TypeName,
 *   GC_PROPS, FieldType, fieldName
 *   ...
 * );
 *
 * Where each GC_PROPS, FieldType, fieldName define a field in the
 * struct with the type FieldType and name fieldName.
 *
 * Where GC_PROPS is either:
 *
 * - NOGC(KIND) -- a no-gc field
 * - GC(KIND) -- a gc-managed field
 *
 * and KIND is either:
 *
 * - FIX -- a fixed-size field
 * - RSZ(len) -- a resizable field, with the length stored in the u32 field `len'
 *
 * e.g.
 *
 * DEFINE_GC_TYPE(
 *   Lambda, 
 *   NOGC(FIX), Closure, parent,
 *   NOGC(FIX), u32, code
 *   NOGC(FIX), u32, capturec,
 *   GC(RSZ(capturec)), Val, captures,
 * );
 */
#define DEFINE_GC_TYPE(Name, ...)                       \
  typedef struct Name {                                 \
    GC_TYPE_EXPAND(                                     \
      GC_TYPE_PASTE(                                    \
        IGNORED,                                        \
        GC_TYPE_STRUCT_FIELD,                           \
        __VA_ARGS__))                                   \
  } Name                                                \
  DECLARE_GC_METADATA(Name, __VA_ARGS__)

#ifndef DO_DECLARE_GC_METADATA
#define DO_DECLARE_GC_METADATA 0
#endif

#define DECLARE_GC_METADATA(...) DECLARE_GC_METADATA0(DO_DECLARE_GC_METADATA, __VA_ARGS__)
#define DECLARE_GC_METADATA0(DO, ...) DECLARE_GC_METADATA1(DO, __VA_ARGS__)
#define DECLARE_GC_METADATA1(DO, ...) GO_DECLARE_GC_METADATA_##DO(__VA_ARGS__)

#define GO_DECLARE_GC_METADATA_0(Name, ...)
#define GO_DECLARE_GC_METADATA_1(Name, ...)             \
  ;                                                     \
  static Field Name##_FIELDS[] = {                      \
    GC_TYPE_EXPAND(                                     \
      GC_TYPE_PASTE(                                    \
        Name,                                           \
        GC_TYPE_FIELD_META,                             \
        __VA_ARGS__))                                   \
  };                                                    \
  static TypeInfo Name##_INFO = {                       \
    .layout = {                                         \
      .align = alignof(Name),                           \
                                                        \
                                                        \
      .size = sizeof(Name)                              \
      GC_TYPE_EXPAND(                                   \
        GC_TYPE_PASTE(                                  \
          Name,                                         \
          GC_RSZ_FIELD_BASED_SIZE,                      \
          __VA_ARGS__)),                                \
      .resizable = {                                    \
        .field = (GC_TYPE_EXPAND(                       \
          GC_TYPE_PASTE(                                \
            IGNORED,                                    \
            GC_COUNT_RSZ,                               \
            __VA_ARGS__)) 0) * 0,                       \
        .offset = GC_TYPE_EXPAND(                       \
          GC_TYPE_PASTE(                                \
            Name,                                       \
            GC_RSZ_OFFSET,                              \
            __VA_ARGS__)) 0,                            \
      },                                                \
      .fieldc = sizeof(Name##_FIELDS) / sizeof(Field),  \
      .fields = Name##_FIELDS,                          \
    },                                                  \
    .protos = {NULL, NULL, 0},                          \
    .name = GS_UTF8_CSTR(#Name)                         \
  }
