/**
  ******************************************************************************
  * @file    sensor.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-15
  * @brief   Re-exposes SensorSample_t (sensor_types.h), qSensorData, and
  *          the TempTask declaration
  ******************************************************************************
  */

#ifndef SENSOR_H
#define SENSOR_H

#include "FreeRTOS.h"
#include "queue.h"
#include "sensor_types.h"

extern QueueHandle_t qSensorData;
void TempTask(void *argument);

#endif /* SENSOR_H */
