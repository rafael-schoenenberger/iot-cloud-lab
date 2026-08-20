cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(firmware C ASM)

set(VENDOR_DIR ${CMAKE_SOURCE_DIR}/../vendor)
set(HAL_DIR ${VENDOR_DIR}/stm32f4xx_hal_driver)
set(CMSIS_DEVICE_DIR ${VENDOR_DIR}/cmsis_device_f4)
set(CMSIS_CORE_DIR ${VENDOR_DIR}/cmsis_core)

set(CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfloat-abi=soft)

add_compile_options(${CPU_FLAGS} -Wall -O0 -g -ffunction-sections -fdata-sections)
add_compile_definitions(STM32F407xx USE_HAL_DRIVER)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${HAL_DIR}/Inc
    ${CMSIS_DEVICE_DIR}/Include
    ${CMSIS_CORE_DIR}/Core/Include
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
    main.c
)
