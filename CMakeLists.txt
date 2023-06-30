cmake_minimum_required(VERSION 3.19)

project(Gliss)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

add_executable(embed_data src/embed_data.c)

function(add_gliss_image
    target_name
    # input_names
  )
  add_custom_command(
    OUTPUT ${target_name}.gi
    COMMAND
    racket
    -nyt
    ${PROJECT_SOURCE_DIR}/src/grt/gliss.rkt
    -f ${PROJECT_SOURCE_DIR}/src/grt/gstd.gs
    -f ${PROJECT_SOURCE_DIR}/src/grt/grt.gs
    -f ${PROJECT_SOURCE_DIR}/src/grt/link.gs
    -- ${CMAKE_CURRENT_BINARY_DIR}/${target_name}.gi
    ${ARGN}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${ARGN}
  )
  add_custom_command(
    OUTPUT ${target_name}.c
    COMMAND embed_data ${target_name} ${target_name}.gi gs_main_data
    DEPENDS ${target_name}.gi embed_data
  )
  add_custom_target(${target_name} DEPENDS ${target_name}.c)
endfunction()

add_gliss_image(link_gi src/grt/gstd.gs src/grt/grt.gs src/grt/link.gs)
add_executable(glissc src/image_driver.c link_gi.c)
target_link_libraries(glissc glissmainrt)

add_executable(gldiss src/bin/gldiss.c)
target_link_libraries(gldiss glissmainrt)

add_subdirectory(test)
