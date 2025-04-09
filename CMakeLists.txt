# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

if (NOT DEFINED PICO_BOARD)
    message(STATUS "PICO_BOARD not set, defaulting to pico2")
    set(PICO_BOARD pico2 CACHE STRING "Board to build for")
else()
    message(STATUS "PICO_BOARD set to ${PICO_BOARD}")
endif()    

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(PicoPendant C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(PicoPendant PicoPendant.cpp)

pico_generate_pio_header(PicoPendant ${CMAKE_CURRENT_LIST_DIR}/quadrature_encoder.pio)

pico_set_program_name(PicoPendant "PicoPendant")

if (DEFINED ENV{PICO_VERSION})
    set(PICO_VERSION $ENV{PICO_VERSION})
else()
    set(PICO_VERSION "0.0.0")
endif()
pico_set_program_version(PicoPendant ${PICO_VERSION})

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(PicoPendant 0)
pico_enable_stdio_usb(PicoPendant 0)

# Add the standard library to the build
target_link_libraries(PicoPendant
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(PicoPendant PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(PicoPendant 
        hardware_pio
        hardware_uart
        )

pico_add_extra_outputs(PicoPendant)
