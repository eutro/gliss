add_executable(rt_test rt_test.c)
target_include_directories(rt_test PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rt_test glissrt)
add_test(NAME "Runtime" COMMAND rt_test)
