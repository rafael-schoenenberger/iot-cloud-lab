/**
  ******************************************************************************
  * @file    sensor_types.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-25
  * @brief   SensorSample_t, split out of sensor.h so it can be included
  *          without pulling in FreeRTOS/queue.h - lets firmware/unit_tests/
  *          compile and use the type natively (plain gcc, no ARM cross-
  *          compilation needed).
  ******************************************************************************
  */

#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <stdint.h>

/**
  * @brief  One TMP108 reading, as pushed onto qSensorData by TempTask and
  *         consumed by ModemTask/modem_build_publish_cmd().
  */
typedef struct {
    int8_t temp_c;      /**< Degrees Celsius, as read from the TMP108. */
    uint32_t uptime_ms; /**< HAL_GetTick() at the moment the sample was read on I2C. */
} SensorSample_t;

#endif /* SENSOR_TYPES_H */
