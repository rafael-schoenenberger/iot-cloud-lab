/**
  ******************************************************************************
  * @file    sensor.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   Sensor sample type, the queue handing samples off to ModemTask,
  *          and the periodic TMP108 read task.
  ******************************************************************************
  */

#ifndef SENSOR_H
#define SENSOR_H

#include "FreeRTOS.h"
#include "queue.h"
#include <stdint.h>

typedef struct {
    int8_t temp_c;
    uint32_t uptime_ms; /* HAL_GetTick() at the moment the sample was read on I2C */
} SensorSample_t;

extern QueueHandle_t qSensorData;

/**
  * @brief  Periodically (every 2s) reads the TMP108 over I2C1, prints the
  *         sample as JSON on UART3, and pushes it onto qSensorData for
  *         ModemTask to publish.
  * @param  argument Unused.
  * @retval None
  */
void TempTask(void *argument);

#endif /* SENSOR_H */
