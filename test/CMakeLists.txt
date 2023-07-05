add_executable(rt_test rt_test.c)
target_include_directories(rt_test PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rt_test glissmainrt)
add_test(NAME "Runtime" COMMAND rt_test)

add_executable(image_test image_test.c)
target_include_directories(image_test PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(image_test glissmainrt)
add_test(NAME "Image" COMMAND image_test)

if(GLISS_WITH_IO)
  add_executable(grt_test grt_test.c)
  target_include_directories(grt_test PUBLIC ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(grt_test glissmainrt)
  add_gliss_image(
    TARGET grt
    INPUTS ../src/grt/gstd.gs ../src/grt/grt.gs
  )
  add_dependencies(grt_test grt_gi)
  add_test(NAME "Gliss Runtime" COMMAND grt_test)
endif()

add_executable(print_sizes print_sizes.c)
target_include_directories(print_sizes PUBLIC ${PROJECT_SOURCE_DIR}/src)
add_test(NAME "Print Sizes" COMMAND print_sizes)

add_executable(gc_test gc_test.c)
target_include_directories(gc_test PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gc_test glissmainrt)
add_test(NAME "Garbage Collector" COMMAND gc_test)

function(add_gliss_test test_name test_camel_name)
  add_gliss_image(
    TARGET ${test_name}_gi
    INPUTS ../src/grt/gstd.gs ./test_harness.gs ./${test_name}.gs ${ARGN}
  )
  add_executable(gliss_${test_name} ../src/image_driver.c ${test_name}_gi.c)
  add_dependencies(gliss_${test_name} ${test_name}_gi_c)
  target_link_libraries(gliss_${test_name} glissmainrt)
  add_test(NAME "Gliss ${test_camel_name}" COMMAND gliss_${test_name})
endfunction()

add_gliss_test(basic_tests "Basic Tests")
add_gliss_test(runtime_tests "Runtime Tests" ../src/grt/grt.gs)
add_gliss_test(gc_tests "Garbage Collector Tests")
