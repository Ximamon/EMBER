add_library(ember_warnings INTERFACE)

option(
    EMBER_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors"
    OFF
)

target_compile_options(
    ember_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
)
if (EMBER_WARNINGS_AS_ERRORS)
    target_compile_options(ember_warnings INTERFACE -Werror)
endif()