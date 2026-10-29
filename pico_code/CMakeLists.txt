cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in the Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico_code C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(main
    main.cpp
    bno055.cpp
    bno055_base.cpp
)

target_link_libraries(main
    pico_stdlib
    hardware_uart
    hardware_i2c
    hardware_irq
)

# USB serial is used to talk to computer_end_code.py.
# UART0 is used by the GPS, so stdio over UART is turned off.
pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)

# Creates main.uf2 to drag onto the Pico
pico_add_extra_outputs(main)
