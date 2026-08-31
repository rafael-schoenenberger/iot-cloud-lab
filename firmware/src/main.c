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
#include <stdio.h>

#define TEMP_TASK_STACK_SIZE  256
#define MODEM_TASK_STACK_SIZE 512
#define SENSOR_QUEUE_LEN      4

/**
  * @brief  Demo task that periodically prints a counter over UART3. Not
  *         started (see the commented-out xTaskCreate() in main() below) -
  *         kept around as a minimal known-good UART3 smoke test
  * @param  argument Unused
  * @retval None
  */
static void HWTask(void *argument)
{
    (void)argument;
    char msg[32];
    int count = 0;

    for(;;)
    {
        vTaskDelay(pdMS_TO_TICKS(2000));
        int len = snprintf(msg, sizeof(msg), "Hello World #%d\r\n", ++count);
        HAL_UART_Transmit(&huart3, (uint8_t *)msg, (uint16_t)len, HAL_MAX_DELAY);
    }
}

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

    // configASSERT(xTaskCreate(HWTask, "HWTask", 256, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);
    configASSERT(xTaskCreate(TempTask, "TempTask", TEMP_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);
    configASSERT(xTaskCreate(ModemTask, "ModemTask", MODEM_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);

    vTaskStartScheduler();

    while(1)
    {
        /* unreachable: vTaskStartScheduler() only returns on failure */
    }
}
