/**
  ******************************************************************************
  * @file    i2c.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   I2C1 initialization for the TMP108 temperature sensor
  ******************************************************************************
  */

#include "i2c.h"
#include "FreeRTOS.h"
#include "task.h"

#define I2C1_CLOCK_SPEED_HZ 100000
#define I2C1_OWN_ADDRESS    0

I2C_HandleTypeDef hi2c1;

/**
  * @brief  HAL MSP init callback: configures the GPIO/clock for I2C1
  * @param  hi2c I2C handle being initialized
  * @retval None
  */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    if(hi2c->Instance != I2C1)
    {
        return;
    }

    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB6 = I2C1_SCL, PB7 = I2C1_SDA (AF4), open-drain as required on an I2C bus */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;

    HAL_GPIO_Init(GPIOB, &gpio);
}

/**
  * @brief  Initializes I2C1 (100 kHz standard mode) used to talk to the
  *         TMP108 temperature sensor
  * @retval None
  */
void i2c1_init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = I2C1_CLOCK_SPEED_HZ;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = I2C1_OWN_ADDRESS;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    configASSERT(HAL_I2C_Init(&hi2c1) == HAL_OK);
}
