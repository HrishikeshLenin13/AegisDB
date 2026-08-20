cmake_minimum_required(VERSION 3.20)
project(AegisDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(aegisql tools/aegisql.cpp)
add_executable(aegis_test tests/test_aegis.cpp)
add_executable(bench_tpch bench/bench_tpch.cpp)
target_link_libraries(bench_tpch PRIVATE sqlite3)

enable_testing()
add_test(NAME aegis_test COMMAND aegis_test)
