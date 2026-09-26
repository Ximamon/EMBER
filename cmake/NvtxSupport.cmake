# cmake/NvtxSupport.cmake

function(ember_configure_nvtx TARGET_NAME)
    # By default, enable NVTX if CUDA is active
    if(NOT DEFINED EMBER_ENABLE_NVTX)
        if(EMBER_ENABLE_CUDA)
            set(EMBER_ENABLE_NVTX ON CACHE BOOL "Enable NVTX ranges for profiling with Nsight Systems")
        else()
            set(EMBER_ENABLE_NVTX OFF CACHE BOOL "Enable NVTX ranges for profiling with Nsight Systems")
        endif()
    endif()

    if(NOT EMBER_ENABLE_NVTX)
        message(STATUS "NVTX profiling disabled.")
        target_compile_definitions(${TARGET_NAME} PUBLIC EMBER_ENABLE_NVTX=0)
        return()
    endif()

    # Buscar cabecera nvtx3/nvToolsExt.h en las rutas del sistema y del Toolkit CUDA
    find_path(NVTX3_INCLUDE_DIR
        NAMES nvtx3/nvToolsExt.h
        HINTS
            ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
            /cm/shared/apps/cuda12.8/toolkit/12.8.1/include
            $ENV{CUDA_HOME}/include
            $ENV{CUDA_PATH}/include
    )

    if(NVTX3_INCLUDE_DIR)
        message(STATUS "NVTX3 found: ${NVTX3_INCLUDE_DIR}")
        
        # SYSTEM prevents internal warnings from third-party headers
        target_include_directories(${TARGET_NAME} SYSTEM PUBLIC ${NVTX3_INCLUDE_DIR})
        target_compile_definitions(${TARGET_NAME} PUBLIC EMBER_ENABLE_NVTX=1)
        
        # NVTX3 dynamically loads libnvToolsExt using dlopen; requires libdl on Linux
        target_link_libraries(${TARGET_NAME} PRIVATE ${CMAKE_DL_LIBS})
    else()
        message(WARNING "EMBER_ENABLE_NVTX is active but doesnt found 'nvtx3/nvToolsExt.h'. Compiling with no-op ranges at zero cost.")
        target_compile_definitions(${TARGET_NAME} PUBLIC EMBER_ENABLE_NVTX=0)
    endif()
endfunction()