add_subdirectory(rt) # pure C runtime
add_subdirectory(driver) # pure C extras for actual execution

if(GLISS_WITH_IO)
  add_subdirectory(bin) # tools and new compiler
endif()

add_subdirectory(test) # tests for the runtime and compiler
