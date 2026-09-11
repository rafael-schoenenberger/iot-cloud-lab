cmake_minimum_required(VERSION 3.20)
project(unit_tests C)

# Built with plain gcc (native, not ARM cross-compiled). Configure/build:
#   cmake -S unit_tests -B build_unit_tests -G Ninja && ninja -C build_unit_tests
# Only the FreeRTOS/HAL-free sources are tested here; the rest of ../src is exercised via Renode.

set(CMAKE_C_STANDARD 11)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/../src)

# Kept in sync with ../src/CMakeLists.txt's -Wall - modem_payload.c/debug_format.c
# are compiled by both this (native) and that (ARM cross) build.
add_compile_options(-Wall)

include_directories(vendor/unity/src)

add_executable(test_modem
    vendor/unity/src/unity.c
    ${SRC_DIR}/modem/modem_payload.c
    modem/test_modem.c
)
target_include_directories(test_modem PRIVATE ${SRC_DIR} ${SRC_DIR}/sensor ${SRC_DIR}/modem)

add_executable(test_debug
    vendor/unity/src/unity.c
    ${SRC_DIR}/debug/debug_format.c
    debug/test_debug.c
)
target_include_directories(test_debug PRIVATE ${SRC_DIR}/debug)
