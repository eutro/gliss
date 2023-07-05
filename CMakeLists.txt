cmake_minimum_required(VERSION 3.19)

project(Gliss)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(GLISS_WITH_IO "Whether to build IO-using tests and binaries" ON)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

include(CTest)

add_library(glissrt
  src/rt.c
  src/driver.c
  src/bytecode/interp.c
  src/bytecode/image.c
  src/bytecode/disass.c
  src/bytecode/primitives.c
  src/gc/gc.c
  src/gc/gc_dump.c
)

add_library(glissmainrt
  src/gs_main_driver.c
)
target_link_libraries(glissmainrt glissrt)

if(NOT GLISS_WITH_IO)
  find_package(GlissTools)
endif()

function(add_gliss_embedded
    target_name
    source_name
    # extra_deps...
  )
  add_custom_command(
    OUTPUT ${target_name}.c
    COMMAND embed_data ${target_name} ${source_name} gs_main_data
    DEPENDS embed_data ${source_name} ${ARGN}
  )
  add_custom_target(${target_name}_c DEPENDS ${target_name}.c)
endfunction()

function(add_gliss_image)
  set(oneValueArgs TARGET LINKER)
  set(multiValueArgs INPUTS)
  cmake_parse_arguments(AGI "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT DEFINED AGI_LINKER)
    set(AGI_LINKER glissc)
  endif()

  add_custom_command(
    OUTPUT ${AGI_TARGET}.gi
    COMMAND ${AGI_LINKER} ${CMAKE_CURRENT_BINARY_DIR}/${AGI_TARGET}.gi ${AGI_INPUTS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${AGI_LINKER} ${AGI_INPUTS}
  )
  add_custom_target(${AGI_TARGET}_gi DEPENDS ${AGI_TARGET}.gi)
  add_gliss_embedded(${AGI_TARGET} ${AGI_TARGET}.gi ${AGI_TARGET}_gi)
endfunction()

if(GLISS_WITH_IO)
  add_executable(embed_data src/embed_data.c)

  add_gliss_embedded(glissc_boot ${CMAKE_CURRENT_SOURCE_DIR}/boot/glissc.gi)
  add_executable(glissc_boot src/image_driver.c glissc_boot.c)
  add_dependencies(glissc_boot glissc_boot_c)
  target_link_libraries(glissc_boot glissmainrt)

  add_gliss_image(
    TARGET glissc
    LINKER glissc_boot
    INPUTS src/grt/gstd.gs src/grt/grt.gs src/grt/link.gs
  )
  add_executable(glissc src/image_driver.c glissc.c)
  add_dependencies(glissc glissc_c)
  target_link_libraries(glissc glissmainrt)

  add_executable(gldiss src/bin/gldiss.c)
  target_link_libraries(gldiss glissmainrt)

  export(TARGETS embed_data glissc gldiss FILE
    "${CMAKE_BINARY_DIR}/GlissToolsConfig.cmake")
endif()

add_subdirectory(test)
