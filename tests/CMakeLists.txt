add_executable(ember_tests
    ember_tests.cpp
)

target_link_libraries(ember_tests PRIVATE ember_core)

add_test(NAME ember_tests COMMAND ember_tests)
if(NOT EMBER_ENABLE_CUDA)
    add_executable(terrain_tests terrain_tests.cpp)
    target_link_libraries(terrain_tests PRIVATE ember_core)
    target_compile_definitions(terrain_tests PRIVATE EMBER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME terrain_tests COMMAND terrain_tests)
endif()

# Build the loader with its CUDA guard enabled; no CUDA runtime linkage is needed.
add_executable(terrain_cuda_guard_tests terrain_cuda_guard_tests.cpp ../src/terrain.cpp ../src/config.cpp)
target_include_directories(terrain_cuda_guard_tests PRIVATE ../include)
target_compile_definitions(terrain_cuda_guard_tests PRIVATE EMBER_ENABLE_CUDA=1)
target_link_libraries(terrain_cuda_guard_tests PRIVATE ember_warnings ember_sanitizers)
add_test(NAME terrain_cuda_guard_tests COMMAND terrain_cuda_guard_tests)
