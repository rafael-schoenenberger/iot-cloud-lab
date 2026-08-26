cmake_minimum_required(VERSION 3.20)
project(unit_tests C)

# Built with plain gcc (not arm-none-eabi-gcc) in the same firmware container
# as the ARM build - native, not cross-compiled. Configure/build separately:
#   cmake -S unit_tests -B build_unit_tests -G Ninja && ninja -C build_unit_tests
# Only modem_payload.c (no FreeRTOS/HAL dependency) is tested here; the rest
# of ../src needs the ARM FreeRTOS port and is exercised via Renode instead.

set(CMAKE_C_STANDARD 11)

# Kept in sync with ../src/CMakeLists.txt's -Wall - modem_payload.c is
# compiled by both this (native) and that (ARM cross) build.
add_compile_options(-Wall)

add_executable(test_modem
    vendor/unity/src/unity.c
    ${CMAKE_SOURCE_DIR}/../src/modem/modem_payload.c
    modem/test_modem.c
)

target_include_directories(test_modem PRIVATE
    vendor/unity/src
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/sensor
    ${CMAKE_SOURCE_DIR}/../src/modem
)
