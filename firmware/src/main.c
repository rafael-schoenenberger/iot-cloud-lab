#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

/* 1 = build for the Renode simulation, 0 = build for real STM32F407 hardware.
 * Renode's stm32f4.repl doesn't map the Cortex-M bit-band alias, so the
 * standard HAL_RCC_OscConfig() PLL enable (__HAL_RCC_PLL_ENABLE(), which
 * writes PLLON through that alias) never sets PLLRDY and hangs in
 * Error_Handler(). Override with -DTARGET_RENODE=0 to build for hardware. */
#ifndef TARGET_RENODE
#define TARGET_RENODE 1
#endif

static UART_HandleTypeDef huart3;

extern void xPortSysTickHandler(void);

void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

static void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    Error_Handler();
}

/* HSE = 25 MHz -> PLL -> SYSCLK = 168 MHz, APB1 = 42 MHz, APB2 = 84 MHz */
static void SystemClock_Config(void)
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

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3) {
        return;
    }

    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB10 = USART3_TX, PB11 = USART3_RX (AF7) */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void uart3_init(void)
{
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart3);
}

static void HWTask(void *argument)
{
    (void)argument;
    char msg[32];
    int count = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        int len = snprintf(msg, sizeof(msg), "Hello World #%d\r\n", ++count);
        HAL_UART_Transmit(&huart3, (uint8_t *)msg, (uint16_t)len, HAL_MAX_DELAY);
    }
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    uart3_init();

    configASSERT(xTaskCreate(HWTask, "HWTask", 256, NULL, tskIDLE_PRIORITY + 1, NULL) == pdPASS);

    vTaskStartScheduler();

    while (1) {
        /* unreachable: vTaskStartScheduler() only returns on failure */
    }
}
