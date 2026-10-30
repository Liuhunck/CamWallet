# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# Prefer the project's existing virtual environment for SDK Python helpers too.
# The Pico extension can otherwise overwrite this with a global interpreter.
if(WIN32)
    set(CAMWALLET_PYTHON "${CMAKE_CURRENT_LIST_DIR}/../../companion/.venv/Scripts/python.exe")
else()
    set(CAMWALLET_PYTHON "${CMAKE_CURRENT_LIST_DIR}/../../companion/.venv/bin/python")
endif()
if(EXISTS "${CAMWALLET_PYTHON}")
    set(Python3_EXECUTABLE "${CAMWALLET_PYTHON}" CACHE FILEPATH "CamWallet venv Python" FORCE)
endif()
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(CamWallet C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(qr)
add_subdirectory(transport)
add_subdirectory(evm)
add_subdirectory(crypto)

# Add executable. Default name is the project name, version 0.1

add_executable(CamWallet
        CamWallet.c
        drivers/lcd.c
        drivers/camera.c
        drivers/touch.c
        drivers/touch_report.c
        ui/scan_preview.c
        ui/evm_review.c
        ui/evm_review_render.c
        third_party/waveshare/font16.c
)

pico_generate_pio_header(CamWallet ${CMAKE_CURRENT_LIST_DIR}/drivers/camera_capture.pio)

pico_set_program_name(CamWallet "CamWallet")
pico_set_program_version(CamWallet "0.1")

# quirc's decoder needs more stack than the SDK's 2 KiB default. The default
# SCRATCH_Y bank is only 4 KiB: carve out the top 32 KiB of main SRAM instead,
# keeping the heap and stack in separate, non-overlapping linker regions.
target_compile_definitions(CamWallet PRIVATE PICO_STACK_SIZE=0x8000)
pico_set_linker_script_var(CamWallet RAM_LENGTH 0x78000)
pico_set_linker_script_var(CamWallet SCRATCH_Y_ORIGIN 0x20078000)
pico_set_linker_script_var(CamWallet SCRATCH_Y_LENGTH 0x8000)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(CamWallet 0)
pico_enable_stdio_usb(CamWallet 1)

# Add the standard library to the build
target_link_libraries(CamWallet
        camwallet_qr
        camwallet_transport
        camwallet_evm
        camwallet_wallet
        pico_rand
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_pwm
        hardware_pio
        hardware_dma)

# Add the standard include files to the build
target_include_directories(CamWallet PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/third_party/waveshare
)

pico_add_extra_outputs(CamWallet)
