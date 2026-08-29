/**
  ******************************************************************************
  * @file    i2c.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   I2C1 handle and initialization for the TMP108 temperature sensor.
  ******************************************************************************
  */

#ifndef I2C_H
#define I2C_H

#include "stm32f4xx_hal.h"

extern I2C_HandleTypeDef hi2c1;

void i2c1_init(void);

#endif /* I2C_H */
