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

option(
  EMBER_ENABLE_MPI
  "Compile with MPI support for multi-node execution"
  ON
)

option(
  EMBER_ENABLE_CUDA
  "Compile with CUDA support for GPUs"
  ON
)

if(EMBER_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD 17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        set(CMAKE_CUDA_ARCHITECTURES 80) # sm_80 for NVIDIA A100 on Curiosity
        message(STATUS "CUDA enabled: ${CMAKE_CUDA_COMPILER}")
    else()
        message(WARNING "CUDA requested but 'nvcc' not found. Disable CUDA.")
        set(EMBER_ENABLE_CUDA OFF CACHE BOOL "" FORCE)
    endif()
endif()

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
include(MpiSupport)
include(CudaSupport)

# Subdirectories
add_subdirectory(src)
add_subdirectory(apps)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()