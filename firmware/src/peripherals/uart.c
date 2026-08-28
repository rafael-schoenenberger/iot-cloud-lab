/**
  ******************************************************************************
  * @file    uart.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   USART3 (debug console) and USART1 (SARA-R412M modem link)
  *          initialization, DMA-driven TX for both, ISR-driven single-byte
  *          RX for USART1, and blocking helpers for reading AT command
  *          responses out of the resulting RX stream buffer.
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

/* UART1/UART3 TX: DMA-driven (DMA2 Stream7 / DMA1 Stream3). modem.c/sensor.c
 * start the transfer without waiting for it to complete. The IRQ handlers
 * below still must run so HAL's internal state resets for the next
 * transmit. */
static DMA_HandleTypeDef hdma_usart1_tx;
static DMA_HandleTypeDef hdma_usart3_tx;

/**
  * @brief  USART1 global interrupt handler, forwarded to the HAL.
  * @retval None
  */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

/**
  * @brief  USART3 global interrupt handler, forwarded to the HAL. Required
  *         so HAL_UART_Transmit_DMA() resets its busy state after the DMA
  *         hands off - see UART_DMATransmitCplt() in stm32f4xx_hal_uart.c.
  * @retval None
  */
void USART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart3);
}

/**
  * @brief  DMA2 Stream7 global interrupt handler (USART1 TX), forwarded to
  *         the HAL.
  * @retval None
  */
void DMA2_Stream7_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_tx);
}

/**
  * @brief  DMA1 Stream3 global interrupt handler (USART3 TX), forwarded to
  *         the HAL.
  * @retval None
  */
void DMA1_Stream3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart3_tx);
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

        /* DMA1 Stream3/Channel4 = USART3_TX (RM0090 Table 42), used by
         * HAL_UART_Transmit_DMA() for the debug prints in sensor.c. */
        __HAL_RCC_DMA1_CLK_ENABLE();

        hdma_usart3_tx.Instance = DMA1_Stream3;
        hdma_usart3_tx.Init.Channel = DMA_CHANNEL_4;
        hdma_usart3_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
        hdma_usart3_tx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_usart3_tx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_usart3_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        hdma_usart3_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        hdma_usart3_tx.Init.Mode = DMA_NORMAL;
        hdma_usart3_tx.Init.Priority = DMA_PRIORITY_LOW;
        hdma_usart3_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        HAL_DMA_Init(&hdma_usart3_tx);
        __HAL_LINKDMA(huart, hdmatx, hdma_usart3_tx);
        HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);

        /* Needed for the DMA-TX completion handshake, not for RX (USART3 is
         * TX-only here) - see USART3_IRQHandler. */
        HAL_NVIC_SetPriority(USART3_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART3_IRQn);
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

        /* DMA2 Stream7/Channel4 = USART1_TX (RM0090 Table 43), used by
         * HAL_UART_Transmit_DMA() for at_send()/at_send_cert() in modem.c. */
        __HAL_RCC_DMA2_CLK_ENABLE();

        hdma_usart1_tx.Instance = DMA2_Stream7;
        hdma_usart1_tx.Init.Channel = DMA_CHANNEL_4;
        hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
        hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        hdma_usart1_tx.Init.Mode = DMA_NORMAL;
        hdma_usart1_tx.Init.Priority = DMA_PRIORITY_LOW;
        hdma_usart1_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        HAL_DMA_Init(&hdma_usart1_tx);
        __HAL_LINKDMA(huart, hdmatx, hdma_usart1_tx);
        HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);

        /* Priority 6 (>= configMAX_SYSCALL_INTERRUPT_PRIORITY): needed for
         * the RX ISR's xStreamBufferSendFromISR() and, like USART3_IRQn
         * above, the DMA-TX completion handshake. */
        HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        return;
    }
}

/**
  * @brief  Initializes USART3 (921600 8N1) used as the debug console.
  * @retval None
  */
void uart3_init(void)
{
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 921600;
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
