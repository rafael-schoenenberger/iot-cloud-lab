/**
  ******************************************************************************
  * @file    main.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-17
  * @brief   Application entry point: HAL/FreeRTOS bring-up, peripheral
  *          initialization and task creation for the temperature-to-AWS-IoT
  *          telemetry pipeline (TempTask -> qSensorData -> ModemTask)
  ******************************************************************************
  */

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "system_clock.h"
#include "uart.h"
#include "i2c.h"
#include "sensor.h"
#include "modem.h"
#include "debug.h"

#define TEMP_TASK_STACK_SIZE  256
#define MODEM_TASK_STACK_SIZE 512
#define DEBUG_TASK_STACK_SIZE 256
#define SENSOR_QUEUE_LEN      4

/**
  * @brief  Application entry point. Brings up the HAL and system clock,
  *         initializes the UART/I2C peripherals, creates the sensor/
  *         debug-log queues and the application tasks, then starts FreeRTOS
  * @retval int never returns except on scheduler-start failure
  */
int main(void)
{
    configASSERT(HAL_Init() == HAL_OK);

    SystemClock_Config();

    uart3_init();
    i2c1_init();
    uart1_init();

    qSensorData = xQueueCreate(SENSOR_QUEUE_LEN, sizeof(SensorSample_t));
    configASSERT(qSensorData != NULL);

    qDebugLog = xQueueCreate(DEBUG_QUEUE_LEN, sizeof(DebugMsg_t));
    configASSERT(qDebugLog != NULL);

    /* FreeRTOS task priority: higher number = more important, range 0
     * (tskIDLE_PRIORITY) to configMAX_PRIORITIES-1 (4 here); opposite of
     * NVIC interrupt priority, see UART_IRQ_PRIORITY in uart.c */
    configASSERT(xTaskCreate(TempTask, "TempTask", TEMP_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);
    configASSERT(xTaskCreate(ModemTask, "ModemTask", MODEM_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);
    configASSERT(xTaskCreate(DebugTask, "DebugTask", DEBUG_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);

    vTaskStartScheduler();

    while(1)
    {
        /* unreachable in practice */
        scheduler_start_failed();
    }
}
