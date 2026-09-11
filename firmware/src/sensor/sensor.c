/**
  ******************************************************************************
  * @file    sensor.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   Periodic TMP108 temperature read task: samples the sensor over
  *          I2C1, logs it on UART3, and hands it off to ModemTask via
  *          qSensorData
  ******************************************************************************
  */

#include "sensor.h"
#include "i2c.h"
#include "debug.h"
#include "stm32f4xx_hal.h"
#include "task.h"

/* TMP108 I2C address (7-bit) and the register offset for its temperature
 * result register (see the TMP108 datasheet) */
#define TMP108_ADDR     0x48
#define TMP108_TEMP_REG 0x00

/* How often TempTask samples/publishes */
#define TEMP_SAMPLE_PERIOD_MS 2000

QueueHandle_t qSensorData;

/**
  * @brief  Periodically (every 2s) reads the TMP108 over I2C1, prints the
  *         sample as JSON on UART3, and pushes it onto qSensorData for
  *         ModemTask to publish
  * @param  argument Unused
  * @retval None
  */
void TempTask(void *argument)
{
    (void)argument;

    for(;;)
    {
        vTaskDelay(pdMS_TO_TICKS(TEMP_SAMPLE_PERIOD_MS));

        int8_t raw;
        if(HAL_I2C_Mem_Read(&hi2c1, TMP108_ADDR << 1, TMP108_TEMP_REG,
                            I2C_MEMADD_SIZE_8BIT, (uint8_t *)&raw, 1,
                            HAL_MAX_DELAY) != HAL_OK)
        {
            continue;
        }

        SensorSample_t sample = { .temp_c = raw, .uptime_ms = HAL_GetTick() };

        DBG("{\"temp_c\":%d,\"uptime_ms\":%lu}\r\n", sample.temp_c, (unsigned long)sample.uptime_ms)

        xQueueSend(qSensorData, &sample, 0); /* 0 timeout: skip if still full */
    }
}
