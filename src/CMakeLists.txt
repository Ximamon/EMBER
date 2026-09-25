add_library(
    ember_core
    cli.cpp
    config.cpp
    export.cpp
    grid.cpp
    terrain.cpp
    runner.cpp
    simulation.cpp
    statistics.cpp
)

# Include public headers
target_include_directories(
    ember_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/simd   
)

# Link global flags
target_link_libraries(
    ember_core
    PUBLIC
        ember_warnings
        ember_sanitizers
)

# Config AVX2 support
ember_configure_simd(ember_core)

# Config MPI support
ember_configure_mpi(ember_core)

# Config CUDA support
ember_configure_cuda(ember_core)

# Config NVTX profiling ranges
ember_configure_nvtx(ember_core)