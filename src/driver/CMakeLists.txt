add_library(glissmainrt
  c/gs_main_driver.c
)
target_link_libraries(glissmainrt glissrt)

set(GLISS_INTERP_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/c/driver.c
  ${CMAKE_CURRENT_SOURCE_DIR}/c/image_driver.c
  PARENT_SCOPE
)

# Create an executable target from Gliss source files.
function(add_gliss_executable target_name)
  add_gliss_image(
    TARGET ${target_name}_gi
    INPUTS ${GLISS_RT_SOURCES} ${ARGN}
  )
  add_executable(${target_name} ${target_name}_gi.c ${GLISS_INTERP_SOURCES})
  add_dependencies(${target_name} ${target_name}_gi_c)
  target_link_libraries(${target_name} glissmainrt)
endfunction()
