cmake_minimum_required(VERSION 3.20)
project(EMBER VERSION 0.5.0 LANGUAGES CXX)

# Cpp cmake settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compile config 
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(
  BUILD_TESTING
  "Compile unitary tests"
  ON
)

option(
  EMBER_ENABLE_AVX2
  "Compile with AVX2 support"
  OFF
)

option(
  EMBER_ENABLE_SANITIZERS
  "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
  OFF
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Load external modules
list(
  APPEND
  CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)
include(CompilerWarnings)
include(Sanitizers)
include(SimdSupport)

# Subdirectories
add_subdirectory(src)
add_subdirectory(apps)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()