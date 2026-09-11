cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(firmware C ASM)

# Kept in sync with unit_tests/CMakeLists.txt's CMAKE_C_STANDARD - modem_payload.c
# and debug_format.c are compiled by both this (ARM cross) and that (native) build.
set(CMAKE_C_STANDARD 11)

set(VENDOR_DIR ${CMAKE_SOURCE_DIR}/../vendor)
set(HAL_DIR ${VENDOR_DIR}/stm32f4xx_hal_driver)
set(CMSIS_DEVICE_DIR ${VENDOR_DIR}/cmsis_device_f4)
set(CMSIS_CORE_DIR ${VENDOR_DIR}/cmsis_core)
set(FREERTOS_DIR ${VENDOR_DIR}/freertos_kernel)

# --- FPU / FreeRTOS port: CURRENT STATE (Renode simulation target) ---
# -mfloat-abi=soft: no FPU instructions are ever emitted, floats (none are
# used yet) would be emulated in software via normal core registers. This
# is paired with FreeRTOS's ARM_CM3 port below (portable/GCC/ARM_CM3),
# NOT ARM_CM4F - the CM4F port's job is lazy-stacking the hardware FPU
# registers (S0-S15/FPSCR) on context switch, which is meaningless (and
# actively refused via a #error in that port.c) without a hard-float ABI.
#
# --- FUTURE STATE for real STM32F407 silicon - change BOTH together: ---
#   1. CPU_FLAGS below: -mfloat-abi=soft  ->  -mfloat-abi=hard -mfpu=fpv4-sp-d16
#      (fpv4-sp-d16 is the correct FPU variant for the Cortex-M4F in the
#      STM32F407; "hard" not "softfp" because everything here - HAL,
#      CMSIS, FreeRTOS - is built from source, so there is no precompiled
#      soft-float library forcing a mismatched calling convention).
#   2. The two ARM_CM3 references below (include dir + port.c source)
#      -> ARM_CM4F, i.e. portable/GCC/ARM_CM4F in both places.
#   3. No other source change needed to enable the FPU itself: CMSIS's
#      SystemInit() in system_stm32f4xx.c already sets SCB->CPACR under
#      an #if (__FPU_PRESENT == 1) && (__FPU_USED == 1) guard, and
#      __FPU_USED is set automatically by the compiler once
#      -mfloat-abi=hard/softfp is active - step 1 alone flips it on.
#   4. Do steps 1+2 together, gated on one single switch (promote
#      TARGET_RENODE, currently a bare #define in main.c, to a real
#      CMake option() that also drives this file) - never change only
#      the flags or only the port, the two must always match.
#   5. Do this ONLY once a task actually needs floating point (e.g. IMU
#      sensor fusion) AND once it's been confirmed that Renode's
#      cpuType: "cortex-m4" model actually emulates the FPU faithfully -
#      that is unverified today, so switching blind would trade a working
#      simulation target for an untested one.
set(CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfloat-abi=soft)

add_compile_options(${CPU_FLAGS} -Wall -O0 -g -ffunction-sections -fdata-sections)
add_compile_definitions(STM32F407xx USE_HAL_DRIVER DEBUG=1)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/modem
    ${CMAKE_SOURCE_DIR}/sensor
    ${CMAKE_SOURCE_DIR}/peripherals
    ${CMAKE_SOURCE_DIR}/debug
    ${HAL_DIR}/Inc
    ${CMSIS_DEVICE_DIR}/Include
    ${CMSIS_CORE_DIR}/Core/Include
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3  # see CPU_FLAGS comment above
)

add_link_options(
    ${CPU_FLAGS}
    -T${CMAKE_SOURCE_DIR}/STM32F407.ld
    -Wl,--gc-sections
    --specs=nano.specs
    --specs=nosys.specs
)

add_executable(firmware.elf
    ${CMSIS_DEVICE_DIR}/Source/Templates/gcc/startup_stm32f407xx.s
    ${CMSIS_DEVICE_DIR}/Source/Templates/system_stm32f4xx.c
    ${HAL_DIR}/Src/stm32f4xx_hal.c
    ${HAL_DIR}/Src/stm32f4xx_hal_rcc.c
    ${HAL_DIR}/Src/stm32f4xx_hal_rcc_ex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_gpio.c
    ${HAL_DIR}/Src/stm32f4xx_hal_dma.c
    ${HAL_DIR}/Src/stm32f4xx_hal_pwr.c
    ${HAL_DIR}/Src/stm32f4xx_hal_pwr_ex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_cortex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_uart.c
    ${HAL_DIR}/Src/stm32f4xx_hal_i2c.c
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM3/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
    main.c
    peripherals/system_clock.c
    peripherals/uart.c
    peripherals/i2c.c
    sensor/sensor.c
    modem/modem.c
    modem/modem_payload.c
    debug/debug.c
    debug/debug_format.c
)
