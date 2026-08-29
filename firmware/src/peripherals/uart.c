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
#include "semphr.h"
#include "stream_buffer.h"

UART_HandleTypeDef huart3;
UART_HandleTypeDef huart1;

/* UART1 RX: interrupt-driven, one byte at a time, fed into a StreamBuffer
 * so ModemTask can block (xStreamBufferReceive) instead of polling for AT
 * responses - see HAL_UART_RxCpltCallback/USART1_IRQHandler below. */
static StreamBufferHandle_t xUart1Rx;
static uint8_t uart1_rx_byte;

/* UART1/UART3 TX: DMA-driven (DMA2 Stream7 / DMA1 Stream3), serialized so a
 * caller blocks until any previous transfer has completed. UART1 uses a task
 * notification (xUart1TxTask) since only ModemTask ever calls it; UART3 uses
 * a real semaphore (xUart3TxSem) since both TempTask and ModemTask do. */
static DMA_HandleTypeDef hdma_usart1_tx;
static DMA_HandleTypeDef hdma_usart3_tx;
static TaskHandle_t xUart1TxTask;
static SemaphoreHandle_t xUart3TxSem;

/**
  * @brief  USART1 global interrupt handler, forwarded to the HAL.
  * @retval None
  */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

/**
  * @brief  USART3 global interrupt handler, forwarded to the HAL - required
  *         for HAL's DMA-TX completion handshake (UART_DMATransmitCplt()).
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
  * @brief  HAL UART RX-complete callback: pushes the received USART1 byte
  *         into the RX stream buffer and re-arms reception for the next one.
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
  * @brief  HAL UART TX-complete callback: frees up USART1/USART3 for the next
  *         uart{1,3}_transmit_dma() call (see xUart1TxTask/xUart3TxSem above).
  * @param  huart UART handle that completed transmission.
  * @retval None
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (huart->Instance == USART1) {
        vTaskNotifyGiveFromISR(xUart1TxTask, &xHigherPriorityTaskWoken);
    } else if (huart->Instance == USART3) {
        xSemaphoreGiveFromISR(xUart3TxSem, &xHigherPriorityTaskWoken);
    } else {
        return;
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
  * @brief  HAL MSP init callback: configures GPIO/DMA/NVIC for whichever
  *         UART instance HAL_UART_Init() was called on.
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

    xUart3TxSem = xSemaphoreCreateBinary();
    configASSERT(xUart3TxSem != NULL);
    xSemaphoreGive(xUart3TxSem); /* starts "available" - no transfer in flight yet */
}

/**
  * @brief  Sends `len` bytes on UART3 via DMA, blocking until any previous
  *         transfer has completed (serialized via xUart3TxSem). Safe to call
  *         from multiple tasks.
  * @param  data Bytes to send.
  * @param  len Number of bytes in `data`.
  * @param  timeout_ms Maximum time to wait for a previous transfer to free
  *         up the UART.
  * @retval true if the transfer was started, false on timeout waiting for
  *         the UART to free up, or a HAL error starting the DMA transfer.
  */
bool uart3_transmit_dma(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (xSemaphoreTake(xUart3TxSem, pdMS_TO_TICKS(timeout_ms)) == pdFALSE) {
        return false;
    }
    if (HAL_UART_Transmit_DMA(&huart3, data, len) != HAL_OK) {
        xSemaphoreGive(xUart3TxSem);
        return false;
    }
    return true;
}

/**
  * @brief  Initializes USART1 (115200 8N1) for the SARA-R412M modem link and
  *         arms interrupt-driven RX into the internal stream buffer.
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
  * @brief  Sends `len` bytes on UART1 via DMA, blocking until any previous
  *         transfer has completed. Must only ever be called from one task
  *         (ModemTask) - see xUart1TxTask above.
  * @param  data Bytes to send.
  * @param  len Number of bytes in `data`.
  * @param  timeout_ms Maximum time to wait for a previous transfer to free
  *         up the UART.
  * @retval true if the transfer was started, false on timeout waiting for
  *         the UART to free up, or a HAL error starting the DMA transfer.
  */
bool uart1_transmit_dma(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (xUart1TxTask == NULL) {
        xUart1TxTask = xTaskGetCurrentTaskHandle();
        xTaskNotifyGive(xUart1TxTask); /* starts "available" - no transfer in flight yet */
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) == 0) {
        return false;
    }
    if (HAL_UART_Transmit_DMA(&huart1, data, len) != HAL_OK) {
        xTaskNotifyGive(xUart1TxTask);
        return false;
    }
    return true;
}

/**
  * @brief  Blocks until `expected` arrives on UART1 or the timeout elapses.
  *         Used only for AT+USECMNG's '>' prompt (not CR/LF-terminated).
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
  *         (leading/blank lines are skipped), or the timeout elapses.
  * @param  line Buffer receiving the line, NUL-terminated, terminator stripped.
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
