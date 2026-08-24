/**
  ******************************************************************************
  * @file    system_clock.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   System clock configuration and the TARGET_RENODE build-target
  *          switch (Renode simulation vs. real STM32F407 hardware).
  ******************************************************************************
  */

#ifndef SYSTEM_CLOCK_H
#define SYSTEM_CLOCK_H

/* 1 = build for the Renode simulation, 0 = build for real STM32F407 hardware.
 * Renode's stm32f4.repl doesn't map the Cortex-M bit-band alias, so the
 * standard HAL_RCC_OscConfig() PLL enable (__HAL_RCC_PLL_ENABLE(), which
 * writes PLLON through that alias) never sets PLLRDY and hangs in
 * Error_Handler(). Override with -DTARGET_RENODE=0 to build for hardware. */
#ifndef TARGET_RENODE
#define TARGET_RENODE 1
#endif

/**
  * @brief  Configures the system clock tree: HSE = 25 MHz -> PLL ->
  *         SYSCLK = 168 MHz, APB1 = 42 MHz, APB2 = 84 MHz. The PLL enable
  *         sequence differs between Renode and real hardware - see
  *         TARGET_RENODE above.
  * @retval None
  */
void SystemClock_Config(void);

#endif /* SYSTEM_CLOCK_H */
