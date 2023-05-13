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

add_library(glissrt src/rt.c src/bytecode/interp.c)

add_executable(gliss src/gliss.cc)
target_link_libraries(gliss glissrt)

add_subdirectory(test)
