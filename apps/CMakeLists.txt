add_executable(ember
    main.cpp
)

target_include_directories(ember PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ember PRIVATE ember_core)