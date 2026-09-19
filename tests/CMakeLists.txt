add_executable(ember_tests
    ember_tests.cpp
)

target_link_libraries(ember_tests PRIVATE ember_core)

add_test(NAME ember_tests COMMAND ember_tests)