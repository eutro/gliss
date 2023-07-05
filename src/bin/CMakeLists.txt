add_executable(embed_data c/embed_data.c)

add_executable(gldiss c/gldiss.c)
target_include_directories(gldiss PRIVATE ${GLISS_RT_INCLUDES})
target_link_libraries(gldiss glissmainrt)

add_gliss_embedded(
  glissc_boot
  ${CMAKE_CURRENT_SOURCE_DIR}/boot/glissc.gi
)
add_executable(glissc_boot glissc_boot.c ${GLISS_INTERP_SOURCES})
add_dependencies(glissc_boot glissc_boot_c)
target_link_libraries(glissc_boot glissmainrt)

add_gliss_image(
  TARGET glissc
  LINKER glissc_boot
  INPUTS ${GLISS_RT_SOURCES} gliss/link.gs
)
add_executable(glissc glissc.c ${GLISS_INTERP_SOURCES})
add_dependencies(glissc glissc_c)
target_link_libraries(glissc glissmainrt)

add_gliss_executable(glissi gliss/interp.gs)

set(GLISS_TOOLS glissc gldiss glissi)
set(GLISS_BUILD_TOOLS ${GLISS_TOOLS} embed_data)
install(
  TARGETS ${GLISS_TOOLS}
  EXPORT GlissTools
  RUNTIME DESTINATION bin
)

export(
  TARGETS ${GLISS_BUILD_TOOLS}
  FILE "${CMAKE_BINARY_DIR}/GlissBuildToolsConfig.cmake"
)

include(CPack)
