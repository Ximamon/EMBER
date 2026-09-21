add_library(ember_warnings INTERFACE)

option(
    EMBER_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors"
    OFF
)

target_compile_options(
    ember_warnings INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wnon-virtual-dtor
        
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
    >
    #-Wold-style-cast
)
if (EMBER_WARNINGS_AS_ERRORS)
    target_compile_options(ember_warnings INTERFACE -Werror)
endif()