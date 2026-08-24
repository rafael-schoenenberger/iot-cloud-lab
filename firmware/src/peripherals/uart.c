/**
  ******************************************************************************
  * @file    uart.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   USART3 (debug console) and USART1 (SARA-R412M modem link)
  *          initialization, ISR-driven single-byte RX for USART1, and
  *          blocking helpers for reading AT command responses out of the
  *          resulting RX stream buffer.
  ******************************************************************************
  */

#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

UART_HandleTypeDef huart3;
UART_HandleTypeDef huart1;

/* UART1 RX: interrupt-driven, one byte at a time, fed into a StreamBuffer
 * so ModemTask can block (xStreamBufferReceive) instead of polling for AT
 * responses - see HAL_UART_RxCpltCallback/USART1_IRQHandler below. */
static StreamBufferHandle_t xUart1Rx;
static uint8_t uart1_rx_byte;

/**
  * @brief  USART1 global interrupt handler, forwarded to the HAL.
  * @retval None
  */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

/**
  * @brief  HAL UART RX-complete callback. Fires once per received byte on
  *         USART1; uart1_init() re-arms HAL_UART_Receive_IT() for the next
  *         byte right after handing this one to ModemTask via the stream
  *         buffer.
  * @param  huart UART handle that completed reception.
  * @retval None
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) {
        return;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xStreamBufferSendFromISR(xUart1Rx, &uart1_rx_byte, 1, &xHigherPriorityTaskWoken);
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
  * @brief  HAL MSP init callback: configures the GPIO/clock/NVIC for
  *         whichever UART instance HAL_UART_Init() was called on.
  * @param  huart UART handle being initialized.
  * @retval None
  */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3) {
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
        return;
    }

    if (huart->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /* PA9 = USART1_TX, PA10 = USART1_RX (AF7) - SARA-R412M modem link */
        GPIO_InitTypeDef gpio = {0};
        gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        gpio.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOA, &gpio);

        /* Priority 6: numerically >= configMAX_SYSCALL_INTERRUPT_PRIORITY,
         * required for the ISR above to safely call xStreamBufferSendFromISR(). */
        HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        return;
    }
}

/**
  * @brief  Initializes USART3 (115200 8N1) used as the debug console.
  * @retval None
  */
void uart3_init(void)
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

/**
  * @brief  Initializes USART1 (115200 8N1) used for the SARA-R412M modem
  *         link, and arms interrupt-driven single-byte reception into the
  *         internal RX stream buffer consumed by uart1_wait_char() and
  *         uart1_read_line().
  * @retval None
  */
void uart1_init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);

    xUart1Rx = xStreamBufferCreate(128, 1);
    configASSERT(xUart1Rx != NULL);
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
}

/**
  * @brief  Blocks (no polling) until `expected` arrives on UART1 or the
  *         timeout elapses. Used only for AT+USECMNG's '>' prompt, which -
  *         unlike every other modem response - is not itself CR/LF-terminated.
  * @param  expected Character to wait for.
  * @param  timeout_ms Maximum time to wait, in milliseconds.
  * @retval true if `expected` was received, false on timeout.
  */
bool uart1_wait_char(char expected, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout_ticks) {
            return false;
        }
        uint8_t byte;
        if (xStreamBufferReceive(xUart1Rx, &byte, 1, timeout_ticks - elapsed) == 0) {
            return false;
        }
        if ((char)byte == expected) {
            return true;
        }
    }
}

/**
  * @brief  Blocks until one full CR/LF-terminated line arrives on UART1
  *         (leading/blank lines, e.g. the CR/LF before every response, are
  *         skipped), or the timeout elapses.
  * @param  line Buffer receiving the line, left NUL-terminated with the
  *         terminator stripped.
  * @param  line_size Size of `line` in bytes.
  * @param  timeout_ms Maximum time to wait, in milliseconds.
  * @retval true if a line was received, false on timeout.
  */
bool uart1_read_line(char *line, size_t line_size, uint32_t timeout_ms)
{
    size_t len = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout_ticks) {
            return false;
        }
        uint8_t byte;
        if (xStreamBufferReceive(xUart1Rx, &byte, 1, timeout_ticks - elapsed) == 0) {
            return false;
        }
        if (byte == '\r' || byte == '\n') {
            if (len == 0) {
                continue;
            }
            line[len] = '\0';
            return true;
        }
        if (len < line_size - 1) {
            line[len++] = (char)byte;
        }
    }
}
