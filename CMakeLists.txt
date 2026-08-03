cmake_minimum_required(VERSION 3.20)

project(EMBER VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(EMBER_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(ember_core
    src/config.cpp
    src/grid.cpp
    src/simulation.cpp
    src/statistics.cpp
    src/runner.cpp
    src/export.cpp
    src/cli.cpp
)

target_include_directories(ember_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ember_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(ember_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(ember_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(EMBER_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(ember_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ember_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(ember src/main.cpp)
target_link_libraries(ember PRIVATE ember_core)

enable_testing()
add_executable(ember_tests tests/ember_tests.cpp)
target_link_libraries(ember_tests PRIVATE ember_core)
add_test(NAME ember_tests COMMAND ember_tests)
