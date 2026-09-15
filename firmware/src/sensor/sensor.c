/**
  ******************************************************************************
  * @file    sensor.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-15
  * @brief   Periodic TMP108 temperature read task: samples the sensor over
  *          I2C1, logs it on UART3, and hands it off to ModemTask via
  *          qSensorData
  ******************************************************************************
  */

#include "sensor.h"
#include "i2c.h"
#include "debug.h"
#include "task.h"

/* TMP108 I2C address (7-bit) and the register offset for its temperature
 * result register (see the TMP108 datasheet) */
#define TMP108_ADDR     0x48
#define TMP108_TEMP_REG 0x00

/* How often TempTask samples/publishes */
#define TEMP_SAMPLE_PERIOD_MS 2000

/**
  * @brief  Queue of SensorSample_t values: filled by TempTask, drained by
  *         ModemTask
  */
QueueHandle_t qSensorData;

/**
  * @brief  Periodically (every 2s) reads the TMP108 over I2C1, pushes the
  *         sample struct onto qSensorData for ModemTask, and logs it
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
            DBG("TempTask: I2C read failed\r\n")
            continue;
        }

        SensorSample_t sample = { .temp_c = raw, .uptime_ms = HAL_GetTick() };

        DBG("{\"temp_c\":%d,\"uptime_ms\":%lu}\r\n", sample.temp_c, (unsigned long)sample.uptime_ms)

        if(xQueueSend(qSensorData, &sample, 0) != pdTRUE) /* 0 timeout: skip if still full */
        {
            DBG("TempTask: qSensorData full, sample dropped\r\n")
        }
    }
}
