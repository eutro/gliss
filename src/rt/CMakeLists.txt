add_library(glissrt
  c/rt.c
  c/logging.c
  c/bytecode/interp.c
  c/bytecode/image.c
  c/bytecode/disass.c
  c/bytecode/primitives.c
  c/gc/gc.c
  c/gc/gc_dump.c
)
target_include_directories(glissrt INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/c
)

set(GLISS_RT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/gliss/gstd.gs
  ${CMAKE_CURRENT_SOURCE_DIR}/gliss/grt.gs
  PARENT_SCOPE
)
