/**
  ******************************************************************************
  * @file    sensor.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   Re-exposes SensorSample_t (see sensor_types.h), the queue
  *          handing samples off to ModemTask, and the periodic TMP108 read
  *          task.
  ******************************************************************************
  */

#ifndef SENSOR_H
#define SENSOR_H

#include "FreeRTOS.h"
#include "queue.h"
#include "sensor_types.h"

/**
  * @brief  Queue of SensorSample_t values: filled by TempTask, drained by
  *         ModemTask.
  */
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
