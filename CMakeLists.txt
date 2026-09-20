cmake_minimum_required(VERSION 3.20)
project(EMBER VERSION 0.5.0 LANGUAGES CXX)

# Cpp cmake settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type: FastCompile (no optimizations, no debug info)
set(
  CMAKE_CXX_FLAGS_PLAIN
  "-O0"
  CACHE STRING "Flags used by the compiler during Plain builds." FORCE
)
set(
  CMAKE_C_FLAGS_PLAIN
  "-O0"
  CACHE STRING "Flags used by the compiler during Plain builds." FORCE
)
set(
  CMAKE_EXE_LINKER_FLAGS_PLAIN 
  "" 
  CACHE STRING "Flags used by the linker during Plain builds." FORCE
)

# Ocultar de la vista básica de cmake-gui/ccmake para no saturar
mark_as_advanced(CMAKE_CXX_FLAGS_PLAIN CMAKE_C_FLAGS_PLAIN CMAKE_EXE_LINKER_FLAGS_PLAIN)

# Registrar "Plain" en la lista de tipos válidos para editores (VS Code, ccmake)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS 
    "Debug" "Release" "RelWithDebInfo" "MinSizeRel" "Plain"
)


# Compile config 
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(
  BUILD_TESTING
  "Compile unitary tests"
  ON
)

option(
  AVX2
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