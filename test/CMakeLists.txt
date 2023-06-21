add_executable(rt_test rt_test.c test_driver.c)
target_include_directories(rt_test PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rt_test glissrt)
add_test(NAME "Runtime" COMMAND rt_test)

add_executable(image_test image_test.c test_driver.c)
target_include_directories(image_test PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(image_test glissrt)
add_test(NAME "Image" COMMAND image_test)
