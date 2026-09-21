function(ember_configure_cuda TARGET_NAME)
    if(NOT EMBER_ENABLE_CUDA)
        target_compile_definitions(${TARGET_NAME} PUBLIC EMBER_ENABLE_CUDA=0)
        return()
    endif()

    # Add CUDA source to target
    target_sources(${TARGET_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/cuda/simulation_cuda.cu
    )

    # Headers from CUDA toolkit
    target_include_directories(${TARGET_NAME} SYSTEM PRIVATE
        ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
    )

    target_compile_definitions(${TARGET_NAME} PUBLIC EMBER_ENABLE_CUDA=1)

    # Flags for nvcc compiler
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:
            -O3
            --use_fast_math
            -Xcompiler -Wall,-Wextra,-Wno-old-style-cast,-Wno-pedantic
        >
    )

    set_target_properties(${TARGET_NAME} PROPERTIES
        CUDA_ARCHITECTURES 80
    )
endfunction()