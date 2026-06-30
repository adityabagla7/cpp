cmake_minimum_required(VERSION 3.16)
project(hft_orderbook CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")

add_compile_options(-Wall -Wextra)
include_directories(include)

add_executable(demo  src/main.cpp)
add_executable(bench bench/benchmark.cpp)

enable_testing()
add_executable(tests tests/test_orderbook.cpp)
add_test(NAME orderbook_tests COMMAND tests)
