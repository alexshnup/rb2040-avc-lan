cmake_minimum_required(VERSION 3.13)

# Import the Pico SDK (specify the correct path to pico_sdk_import.cmake)
include(pico_sdk_import.cmake)

project(avc_lan_capture_project)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Add the executable
add_executable(avc_lan_capture
    main.c
    avc_lan_capture.pio
)

# Generate header files from the PIO program
pico_generate_pio_header(avc_lan_capture ${CMAKE_CURRENT_LIST_DIR}/avc_lan_capture.pio)

# Link necessary libraries
target_link_libraries(avc_lan_capture
    pico_stdlib
    hardware_pio
    hardware_clocks
)

# Enable USB connection, disable standard UART output
pico_enable_stdio_usb(avc_lan_capture 1)
pico_enable_stdio_uart(avc_lan_capture 0)

# Add additional output files (UF2, BIN, etc.)
pico_add_extra_outputs(avc_lan_capture)
