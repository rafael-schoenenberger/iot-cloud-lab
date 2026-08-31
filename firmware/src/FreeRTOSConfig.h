/**
  ******************************************************************************
  * @file    FreeRTOSConfig.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   Project-specific FreeRTOS kernel configuration: tick rate, task
  *          priorities/stack/heap sizing, NVIC priority grouping for the
  *          Cortex-M4/ARM_CM3 port, and the configASSERT() failure trap
  ******************************************************************************
  */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t SystemCoreClock;

#define configUSE_PREEMPTION                   1
#define configUSE_IDLE_HOOK                    0
#define configUSE_TICK_HOOK                    0
#define configCPU_CLOCK_HZ                     (SystemCoreClock)
#define configTICK_RATE_HZ                     1000
#define configMAX_PRIORITIES                   5
#define configMINIMAL_STACK_SIZE               128
#define configTOTAL_HEAP_SIZE                  (10 * 1024)
#define configMAX_TASK_NAME_LEN                16
#define configUSE_16_BIT_TICKS                 0
#define configIDLE_SHOULD_YIELD                1
#define configUSE_MUTEXES                      1
#define configUSE_RECURSIVE_MUTEXES            0
#define configUSE_COUNTING_SEMAPHORES          1
#define configUSE_TIMERS                       1
#define configTIMER_TASK_PRIORITY              (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH               10
#define configTIMER_TASK_STACK_DEPTH           configMINIMAL_STACK_SIZE
#define configCHECK_FOR_STACK_OVERFLOW         2
#define configUSE_MALLOC_FAILED_HOOK           0
#define configQUEUE_REGISTRY_SIZE              0
#define configUSE_TRACE_FACILITY               0
#define configGENERATE_RUN_TIME_STATS          0

#define INCLUDE_vTaskPrioritySet               1
#define INCLUDE_uxTaskPriorityGet              1
#define INCLUDE_vTaskDelete                    1
#define INCLUDE_vTaskSuspend                   1
#define INCLUDE_vTaskDelay                     1
#define INCLUDE_xTaskGetSchedulerState         1
#define INCLUDE_xTaskGetCurrentTaskHandle      1

/* Cortex-M4 with 4 implemented NVIC priority bits. Uses the ARM_CM3 port
 * (not ARM_CM4F), since CMakeLists.txt builds with -mfloat-abi=soft - no
 * hardware FPU context to lazy-stack, and CM3 is FreeRTOS's documented
 * choice for a non-FPU build. */
#define configPRIO_BITS                                4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY        0xf
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY   5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configASSERT(x) do { if ((x) == 0) { taskDISABLE_INTERRUPTS(); for( ;; ); } } while (0)

/* Satisfies the weak SVC_Handler/PendSV_Handler symbols in
 * startup_stm32f407xx.s. SysTick_Handler is intentionally not remapped -
 * system_clock.c defines it itself to also drive HAL_IncTick(). */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
