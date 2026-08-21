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

/* Cortex-M4 with 4 implemented NVIC priority bits (matches Renode's
 * nvic.priorityMask: 0xF0 and TICK_INT_PRIORITY in stm32f4xx_hal_conf.h).
 * The kernel uses the ARM_CM3 port (not ARM_CM4F): CPU_FLAGS in
 * CMakeLists.txt build with -mfloat-abi=soft, so there is no hardware FPU
 * context to lazy-stack. The CM3 port is architecturally identical for a
 * non-FPU build and is FreeRTOS's documented choice for that case. */
#define configPRIO_BITS                                4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY        0xf
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY   5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configASSERT(x) if ((x) == 0) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/* Let the FreeRTOS port implementations satisfy the weak SVC_Handler/
 * PendSV_Handler symbols in startup_stm32f407xx.s. SysTick_Handler is
 * intentionally NOT remapped here - main.c defines it itself so it can
 * drive both HAL_IncTick() and xPortSysTickHandler(). */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
