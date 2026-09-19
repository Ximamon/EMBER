add_library(ember_sanitizers INTERFACE)

if(EMBER_ENABLE_SANITIZERS)
    message(STATUS "Configuring ASan and UBSan")
    target_compile_options(
        ember_sanitizers INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(
        ember_sanitizers INTERFACE
        -fsanitize=address,undefined
    )
endif()