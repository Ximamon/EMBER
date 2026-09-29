add_library(
    ember_core
    cli.cpp
    config.cpp
    export.cpp
    fuel_model.cpp
    grid.cpp
    rothermel.cpp
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
)

# Link global flags
target_link_libraries(
    ember_core
    PUBLIC
        ember_warnings
        ember_sanitizers
)
if(WIN32)
    target_compile_definitions(ember_core PUBLIC NOMINMAX)
endif()

# Config AVX2 support
ember_configure_simd(ember_core)

# Config MPI support
ember_configure_mpi(ember_core)

# Config CUDA support
ember_configure_cuda(ember_core)

find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_link_libraries(ember_core PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(ember_core PUBLIC EMBER_HAVE_OPENMP=1)
else()
    target_compile_definitions(ember_core PUBLIC EMBER_HAVE_OPENMP=0)
endif()

# Config NVTX profiling ranges
ember_configure_nvtx(ember_core)
