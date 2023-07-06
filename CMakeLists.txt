cmake_minimum_required(VERSION 3.19)

project(Gliss)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

if(EMSCRIPTEN)
  add_link_options(-sALLOW_MEMORY_GROWTH)
endif()

include(CTest)

# If we are cross-compiling, we need to import the native tools from
# elsewhere, otherwise we don't have a compiler.
if(CMAKE_CROSSCOMPILING)
  find_package(GlissBuildTools)
endif()

# Create a ${target_name}_c target that builds a ${target_name}.c file which
# embeds the binary data from the given sources.
function(add_gliss_embedded
    target_name
    source_name
    # extra_deps...
  )
  add_custom_command(
    OUTPUT ${target_name}.c
    COMMAND embed_data ${target_name}.c ${source_name} gs_main_data
    DEPENDS embed_data ${source_name} ${ARGN}
  )
  add_custom_target(${target_name}_c DEPENDS ${target_name}.c)
endfunction()

# Create a ${target_name}_gi target that compiles the given Gliss source files
# into an image. Also calls `add_gliss_embedded` to create a ${target_name}_ci
# target.
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

add_subdirectory(src)
