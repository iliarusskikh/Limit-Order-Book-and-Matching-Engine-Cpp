cmake_minimum_required(VERSION 3.16)
# Limit Order Book & Matching Engine — C++17, zero third-party deps.
# Targets: matcher (CLI), lob_tests (G1–G9 harness).
project(LimitOrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lob_core INTERFACE)
target_include_directories(lob_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(lob_core INTERFACE
  -Wall -Wextra -Wpedantic
)

# CLI: run a scenario file (Stage D will flesh this out)
add_executable(matcher src/main.cpp)
target_link_libraries(matcher PRIVATE lob_core)

# Golden / unit tests
add_executable(lob_tests tests/test_matching.cpp)
target_link_libraries(lob_tests PRIVATE lob_core)

enable_testing()
add_test(NAME lob_tests COMMAND lob_tests)
