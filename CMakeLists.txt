cmake_minimum_required(VERSION 4.0)
project(serdex)

set(CMAKE_CXX_STANDARD 20)

add_library(serdex STATIC serdex.cpp)
add_executable(serdex_test test.cpp)
target_link_libraries(serdex_test serdex)