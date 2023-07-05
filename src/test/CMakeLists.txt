function(add_c_test test_name test_camel_name)
  add_executable(${test_name} c/${test_name}.c)
  target_link_libraries(${test_name} glissmainrt)
  add_test(NAME "C ${test_camel_name}" COMMAND ${test_name})
endfunction()

function(add_gliss_test test_name test_camel_name)
  add_gliss_executable(gliss_${test_name}
    gliss/test_harness.gs
    gliss/${test_name}.gs
  )
  add_test(NAME "Gliss ${test_camel_name}" COMMAND gliss_${test_name})
endfunction()

add_executable(print_sizes c/print_sizes.c)
target_link_libraries(print_sizes glissrt) # for includes
add_test(NAME "C Print Sizes" COMMAND print_sizes)

add_c_test(rt_test "Runtime Tests")
add_c_test(image_test "Image Tests")
add_c_test(gc_test "Garbage Collector Tests")

add_gliss_test(basic_tests "Basic Tests")
add_gliss_test(runtime_tests "Runtime Tests")
add_gliss_test(gc_tests "Garbage Collector Tests")
