add_library(ember_warnings INTERFACE)

option(
    EMBER_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors"
    OFF
)

if(MSVC)
    target_compile_options(ember_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ember_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wnon-virtual-dtor
        
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
    >)
endif()
if (EMBER_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(ember_warnings INTERFACE /WX)
    else()
        target_compile_options(ember_warnings INTERFACE -Werror)
    endif()
endif()
