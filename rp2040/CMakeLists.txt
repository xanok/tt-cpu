cmake_minimum_required(VERSION 3.13)

# Build for the current demo board (RP2350B, the default):
#   cmake -B build -DPICO_SDK_PATH=~/tools/pico-sdk && cmake --build build
# Build for the older RP2040 demo board (also used by emutest/):
#   cmake -B build_rp2040 -DTT_BOARD=rp2040 -DPICO_SDK_PATH=~/tools/pico-sdk
# Build for a Raspberry Pi Pico 2 on the bench (docs/bench.md):
#   cmake -B build_pico2 -DTT_BOARD=pico2 -DPICO_SDK_PATH=~/tools/pico-sdk
set(TT_BOARD rp2350 CACHE STRING "Board: rp2350 (TT DB v3 / ETR), rp2040 (TT06+) or pico2 (bench test)")
if (TT_BOARD STREQUAL "rp2350")
    set(PICO_BOARD tt_demo_rp2350 CACHE STRING "Board type")
    set(TT_BOARD_DEF TT_BOARD_RP2350)
elseif (TT_BOARD STREQUAL "rp2040")
    set(PICO_BOARD tt_demo_rp2040 CACHE STRING "Board type")
    # 200 MHz system clock (officially supported by RP2040; the SDK raises the
    # core voltage to 1.15 V for it). Gives the spi-ram-emu timing more margin.
    set(TT_BOARD_DEF TT_BOARD_RP2040 SYS_CLK_MHZ=200)
elseif (TT_BOARD STREQUAL "pico2")
    # bench test: Raspberry Pi Pico 2 wired to a Tang Nano 20K (docs/bench.md)
    set(PICO_BOARD pico2 CACHE STRING "Board type")
    set(TT_BOARD_DEF TT_BOARD_PICO2)
else()
    message(FATAL_ERROR "TT_BOARD must be rp2350, rp2040 or pico2")
endif()
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
target_compile_definitions(${NAME} PRIVATE ${TT_BOARD_DEF})
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

# ---- emu_test: only the SPI RAM emulator, for the rp2040js replay test
# (emutest/). Same sram.c and sram.pio as the firmware above; the service
# loop runs on core0 because rp2040js has no second core.
add_executable(emu_test
    emutest/main.c
    spi-ram-emu/sram.c
)
target_include_directories(emu_test PRIVATE src spi-ram-emu)
target_compile_options(emu_test PRIVATE -Wall -Wextra)
target_compile_definitions(emu_test PRIVATE PIN_READY=25 ${TT_BOARD_DEF})
target_link_libraries(emu_test
    pico_stdlib
    pico_multicore_headers
    hardware_dma
    hardware_pio
)
pico_generate_pio_header(emu_test ${CMAKE_CURRENT_LIST_DIR}/spi-ram-emu/sram.pio)
pico_set_linker_script_var(emu_test RAM_LENGTH 0x30000)
pico_add_linker_script_override_path(emu_test ${CMAKE_CURRENT_LIST_DIR}/spi-ram-emu/ld
    FILES memory_extra.incl section_extra_post_data.incl)
pico_enable_stdio_usb(emu_test 0)
pico_enable_stdio_uart(emu_test 0)
pico_add_extra_outputs(emu_test)

# ---- emu_test_stock: emu_test with the upstream MISO timing (the next bit
# is shifted out right after the RISING SCK edge), to check the fallback
# "stock timing + MISO_RISE strap = 1" in the same test. The .pio file is
# generated from sram.pio by undoing the one timing change.
file(READ ${CMAKE_CURRENT_LIST_DIR}/spi-ram-emu/sram.pio SRAM_PIO_TEXT)
string(REPLACE
"    wait 0 pin SRAM_SCK_OFS     ; cpu8 change: shift out after the falling edge
    out pins, 1
    wait 1 pin SRAM_SCK_OFS
    jmp write_loop"
"    out pins, 1
    wait 0 pin SRAM_SCK_OFS
    wait 1 pin SRAM_SCK_OFS
    jmp write_loop"
    SRAM_PIO_STOCK "${SRAM_PIO_TEXT}")
if (SRAM_PIO_STOCK STREQUAL SRAM_PIO_TEXT)
    message(FATAL_ERROR "sram.pio: write loop not found, cannot make the stock-timing variant")
endif()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/stock)
file(WRITE ${CMAKE_BINARY_DIR}/stock/sram.pio "${SRAM_PIO_STOCK}")

add_executable(emu_test_stock
    emutest/main.c
    spi-ram-emu/sram.c
)
target_include_directories(emu_test_stock PRIVATE src spi-ram-emu)
target_compile_definitions(emu_test_stock PRIVATE PIN_READY=25 ${TT_BOARD_DEF})
target_link_libraries(emu_test_stock
    pico_stdlib
    pico_multicore_headers
    hardware_dma
    hardware_pio
)
# its own output folder: the default one already holds the real sram.pio.h
pico_generate_pio_header(emu_test_stock ${CMAKE_BINARY_DIR}/stock/sram.pio
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/stock)
pico_set_linker_script_var(emu_test_stock RAM_LENGTH 0x30000)
pico_add_linker_script_override_path(emu_test_stock ${CMAKE_CURRENT_LIST_DIR}/spi-ram-emu/ld
    FILES memory_extra.incl section_extra_post_data.incl)
pico_enable_stdio_usb(emu_test_stock 0)
pico_enable_stdio_uart(emu_test_stock 0)
pico_add_extra_outputs(emu_test_stock)

# ---- loop_test, loop_test_stock: loopback test of the emulator on a real
# Pico 2 (emutest/loop.c, emutest/README.md). The third PIO block plays the
# chip on GP2..GP5; four wires join them to the emulator's pins. Pico 2 only.
if (TT_BOARD STREQUAL "pico2")
    foreach(V IN ITEMS "" "_stock")
        add_executable(loop_test${V} emutest/loop.c spi-ram-emu/sram.c)
        target_include_directories(loop_test${V} PRIVATE src spi-ram-emu)
        target_compile_options(loop_test${V} PRIVATE -Wall -Wextra)
        target_compile_definitions(loop_test${V} PRIVATE ${TT_BOARD_DEF})
        target_link_libraries(loop_test${V} pico_stdlib pico_multicore hardware_dma hardware_pio)
        if (V STREQUAL "")
            pico_generate_pio_header(loop_test ${CMAKE_CURRENT_LIST_DIR}/spi-ram-emu/sram.pio)
            pico_generate_pio_header(loop_test ${CMAKE_CURRENT_LIST_DIR}/emutest/loop.pio)
        else()
            target_compile_definitions(loop_test_stock PRIVATE LOOP_STOCK)
            pico_generate_pio_header(loop_test_stock ${CMAKE_BINARY_DIR}/stock/sram.pio OUTPUT_DIR ${CMAKE_BINARY_DIR}/stock)
            pico_generate_pio_header(loop_test_stock ${CMAKE_CURRENT_LIST_DIR}/emutest/loop.pio OUTPUT_DIR ${CMAKE_BINARY_DIR}/stock)
        endif()
        pico_set_linker_script_var(loop_test${V} RAM_LENGTH 0x30000)
        pico_add_linker_script_override_path(loop_test${V} ${CMAKE_CURRENT_LIST_DIR}/spi-ram-emu/ld
            FILES memory_extra.incl section_extra_post_data.incl)
        pico_enable_stdio_usb(loop_test${V} 1)
        pico_enable_stdio_uart(loop_test${V} 0)
        pico_add_extra_outputs(loop_test${V})
    endforeach()
endif()
