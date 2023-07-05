add_subdirectory(rt) # pure C runtime
add_subdirectory(driver) # pure C extras for actual execution

if(NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(bin) # tools and new compiler
endif()

add_subdirectory(test) # tests for the runtime and compiler
