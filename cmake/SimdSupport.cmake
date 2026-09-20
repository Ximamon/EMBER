function(ember_configure_simd TARGET_NAME)
    if(NOT AVX2)
        target_compile_definitions(
            ${TARGET_NAME}
            PRIVATE
            AVX2=0
        )
        return()
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        message(
            STATUS
            "AVX2 enabled on: ${TARGET_NAME}"
        )

        # Add sources directly to target
        target_sources(
            ${TARGET_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/src/simd/avx2_support.cpp
            ${PROJECT_SOURCE_DIR}/src/simd/simulation_avx2.cpp
        )

        target_compile_options(${TARGET_NAME} PRIVATE -mavx2)

        target_compile_definitions(${TARGET_NAME} PRIVATE AVX2=1)
    else()
        message(
            WARNING
            "AVX2 not supported on ${CMAKE_SYSTEM_PROCESSOR} architecture. SIMD compile path disabled"
        )
        target_compile_definitions(${TARGET_NAME} PRIVATE AVX2=0)
    endif()
endfunction()