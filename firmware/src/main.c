/**
  ******************************************************************************
  * @file    main.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
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
  *         initializes the UART/I2C peripherals, creates the sensor queue
  *         and the application tasks, then starts the FreeRTOS scheduler
  * @retval int Never returns - vTaskStartScheduler() only returns on failure,
  *         in which case execution falls into the trailing infinite loop
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

    configASSERT(xTaskCreate(TempTask, "TempTask", TEMP_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);
    configASSERT(xTaskCreate(ModemTask, "ModemTask", MODEM_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);
    configASSERT(xTaskCreate(DebugTask, "DebugTask", DEBUG_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);

    vTaskStartScheduler();

    while(1)
    {
        /* unreachable: vTaskStartScheduler() only returns on failure */
    }
}
