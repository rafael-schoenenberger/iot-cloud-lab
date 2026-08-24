/**
  ******************************************************************************
  * @file    sensor.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   Periodic TMP108 temperature read task: samples the sensor over
  *          I2C1, logs it on UART3, and hands it off to ModemTask via
  *          qSensorData.
  ******************************************************************************
  */

#include "sensor.h"
#include "uart.h"
#include "i2c.h"
#include "stm32f4xx_hal.h"
#include "task.h"
#include <stdio.h>

#define TMP108_ADDR 0x48

QueueHandle_t qSensorData;

/**
  * @brief  Periodically (every 2s) reads the TMP108 over I2C1, prints the
  *         sample as JSON on UART3, and pushes it onto qSensorData for
  *         ModemTask to publish.
  * @param  argument Unused.
  * @retval None
  */
void TempTask(void *argument)
{
    (void)argument;
    char msg[64];

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        int8_t raw;
        if (HAL_I2C_Mem_Read(&hi2c1, TMP108_ADDR << 1, 0x00,
                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)&raw, 1,
                              HAL_MAX_DELAY) != HAL_OK) {
            continue;
        }

        SensorSample_t sample = { .temp_c = raw, .uptime_ms = HAL_GetTick() };

        int len = snprintf(msg, sizeof(msg), "{\"temp_c\":%d,\"uptime_ms\":%lu}\r\n",
                            sample.temp_c, (unsigned long)sample.uptime_ms);
        HAL_UART_Transmit(&huart3, (uint8_t *)msg, (uint16_t)len, HAL_MAX_DELAY);

        xQueueSend(qSensorData, &sample, 0); /* 0 timeout: skip if still full */
    }
}
