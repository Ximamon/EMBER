option(
    EMBER_ENABLE_MPI 
    "Enable MPI support for multi-node execution" 
    OFF
)

function(ember_configure_mpi TARGET_NAME)
    if(EMBER_ENABLE_MPI)
        find_package(MPI REQUIRED)
        message(
            STATUS 
            "MPI Enabled: ${MPI_CXX_COMPILER}"
        )
        
        # Enlazar las bibliotecas MPI al target indicado
        target_link_libraries(${TARGET_NAME} PUBLIC MPI::MPI_CXX)
        
        # Definición para compilar bloques condicionales en C++
        target_compile_definitions(${TARGET_NAME} PUBLIC EMBER_ENABLE_MPI=1)
    else()
        message(
            STATUS 
            "MPI Disabled: Compiling single-node executable"
        )
        target_compile_definitions(${TARGET_NAME} PUBLIC EMBER_ENABLE_MPI=0)
    endif()
endfunction()