/**
  ******************************************************************************
  * @file    system_clock.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   System clock configuration and the TARGET_RENODE build-target
  *          switch (Renode simulation vs. real STM32F407 hardware)
  ******************************************************************************
  */

#ifndef SYSTEM_CLOCK_H
#define SYSTEM_CLOCK_H

/* 1 = build for Renode, 0 = for real STM32F407 hardware. Renode doesn't map
 * the Cortex-M bit-band alias, so HAL's PLL-enable write through it never
 * sets PLLRDY and hangs. Override with -DTARGET_RENODE=0 for hardware */
#ifndef TARGET_RENODE
#define TARGET_RENODE 1
#endif

void SystemClock_Config(void);

#endif /* SYSTEM_CLOCK_H */
