/**
  ******************************************************************************
  * @file    system_clock.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   SysTick handler wiring for the FreeRTOS tick, the fault trap used
  *          across the application, and the HSE -> PLL clock tree bring-up
  *          (Renode simulation vs. real hardware, see TARGET_RENODE).
  ******************************************************************************
  */

#include "system_clock.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

/**
  * @brief  SysTick interrupt handler. Advances the HAL tick and, once the
  *         FreeRTOS scheduler has been started, forwards to the FreeRTOS
  *         port's own SysTick handler.
  * @retval None
  */
void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

/**
  * @brief  Traps execution on an unrecoverable initialization failure.
  * @retval None
  */
static void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}

/**
  * @brief  FreeRTOS stack-overflow hook. Routed straight into Error_Handler().
  * @param  xTask Handle of the task whose stack overflowed.
  * @param  pcTaskName Name of the task whose stack overflowed.
  * @retval None
  */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    Error_Handler();
}

/**
  * @brief  Configures the system clock tree: HSE = 25 MHz -> PLL ->
  *         SYSCLK = 168 MHz, APB1 = 42 MHz, APB2 = 84 MHz. The PLL enable
  *         sequence differs between Renode and real hardware - see
  *         TARGET_RENODE above.
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;

#if TARGET_RENODE
    /* Bring up HSE only through HAL_RCC_OscConfig(); the PLL is started
     * manually below via plain register writes (see TARGET_RENODE above). */
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    WRITE_REG(RCC->PLLCFGR, RCC_PLLSOURCE_HSE | 25U
              | (336U << RCC_PLLCFGR_PLLN_Pos)
              | (((RCC_PLLP_DIV2 >> 1U) - 1U) << RCC_PLLCFGR_PLLP_Pos)
              | (7U << RCC_PLLCFGR_PLLQ_Pos));
    SET_BIT(RCC->CR, RCC_CR_PLLON);
    {
        uint32_t tickstart = HAL_GetTick();
        while (READ_BIT(RCC->CR, RCC_CR_PLLRDY) == 0U) {
            if ((HAL_GetTick() - tickstart) > 100U) {
                Error_Handler();
            }
        }
    }
#else
    /* Real hardware: the standard HAL path works fine since the CPU's
     * bit-band alias is actually wired up. */
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 25;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }
#endif

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                 | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}
