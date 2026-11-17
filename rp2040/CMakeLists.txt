cmake_minimum_required(VERSION 3.13)

# Build: cmake -B build -DPICO_SDK_PATH=~/tools/pico-sdk && cmake --build build
set(PICO_BOARD tt_demo_rp2040 CACHE STRING "Board type")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)  # spi-ram-emu timing needs optimised code
endif()

include(pico_sdk_import.cmake)
project(cpu8_loader C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(NAME cpu8_loader)
add_executable(${NAME}
    src/main.c
    src/proto.c
    src/hw_pico.c
    spi-ram-emu/sram.c
)
target_include_directories(${NAME} PRIVATE src spi-ram-emu)
target_compile_options(${NAME} PRIVATE -Wall -Wextra)
# 200 MHz system clock (officially supported by RP2040; the SDK raises the
# core voltage to 1.15 V for it). Needed for the spi-ram-emu timing margin.
target_compile_definitions(${NAME} PRIVATE SYS_CLK_MHZ=200)
target_link_libraries(${NAME}
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_pio
    hardware_pwm
)

pico_generate_pio_header(${NAME} ${CMAKE_CURRENT_LIST_DIR}/spi-ram-emu/sram.pio)
# Reserve the top 64 KB of SRAM (0x20030000) for the emulated SPI RAM.
pico_set_linker_script_var(${NAME} RAM_LENGTH 0x30000)
pico_add_linker_script_override_path(${NAME} ${CMAKE_CURRENT_LIST_DIR}/spi-ram-emu/ld
    FILES memory_extra.incl section_extra_post_data.incl)

pico_enable_stdio_usb(${NAME} 1)
pico_enable_stdio_uart(${NAME} 0)
pico_add_extra_outputs(${NAME})
