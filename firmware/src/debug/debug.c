/**
  ******************************************************************************
  * @file    debug.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-17
  * @brief   Dedicated debug-output task: DebugTask alone owns UART3 debug
  *          output, draining qDebugLog and printing each message via DMA -
  *          see debug_log()
  ******************************************************************************
  */

#include "debug.h"
#include "debug_format.h"
#include "uart.h"
#include "stm32f4xx_hal.h"
#include "task.h"
#include <stdarg.h>

/* Generous vs. millisecond-scale UART3 DMA transfers - only matters if a
 * previous transfer never completes (e.g. a hardware fault) */
#define UART3_TX_TIMEOUT_MS 1000

/* How often DebugTask wakes up to drain qDebugLog */
#define DEBUG_POLL_PERIOD_MS 100

/* DebugTask's "[<uptime_ms>] <text>" line buffer - DEBUG_MSG_LEN for the
 * text plus room for the "[4294967295] " uptime_ms prefix (uint32_t max) */
#define DEBUG_LINE_LEN (DEBUG_MSG_LEN + 16)

/**
  * @brief  Queue of DebugMsg_t values: filled by DBG(), drained by DebugTask
  */
QueueHandle_t qDebugLog;

/**
  * @brief  Formats a debug line and enqueues it for DebugTask to print on
  *         UART3. Drops the message if the queue is full rather than
  *         blocking the caller
  * @param  fmt printf-style format string
  * @retval None
  */
void debug_log(const char *fmt, ...)
{
    DebugMsg_t msg;
    msg.uptime_ms = HAL_GetTick();

    va_list args;
    va_start(args, fmt);
    msg.len = (uint16_t)debug_format_msg(msg.text, sizeof(msg.text), fmt, args);
    va_end(args);

    xQueueSend(qDebugLog, &msg, 0); /* 0 timeout: drop if DebugTask can't keep up */
}

/**
  * @brief  ISR-safe variant of debug_log(), for use from interrupt context
  *         (e.g. HAL_UART_RxCpltCallback). Drops the message if the queue is
  *         full rather than blocking
  * @param  pxHigherPriorityTaskWoken Passed through to xQueueSendFromISR()
  * @param  fmt printf-style format string
  * @retval None
  */
void debug_log_from_isr(BaseType_t *pxHigherPriorityTaskWoken, const char *fmt, ...)
{
    DebugMsg_t msg;
    msg.uptime_ms = HAL_GetTick();

    va_list args;
    va_start(args, fmt);
    msg.len = (uint16_t)debug_format_msg(msg.text, sizeof(msg.text), fmt, args);
    va_end(args);

    xQueueSendFromISR(qDebugLog, &msg, pxHigherPriorityTaskWoken);
}

/**
  * @brief  Wakes up every DEBUG_POLL_PERIOD_MS and drains up to
  *         DEBUG_QUEUE_LEN messages from qDebugLog onto UART3 via DMA,
  *         falling back to uart3_panic_write() on overflow or a failed send
  * @param  argument Unused
  * @retval None
  */
void DebugTask(void *argument)
{
    (void)argument;
    DebugMsg_t msg;
    char line[DEBUG_LINE_LEN];

    for(;;)
    {
        vTaskDelay(pdMS_TO_TICKS(DEBUG_POLL_PERIOD_MS));

        int dequeued = 0;
        while(dequeued < DEBUG_QUEUE_LEN && xQueueReceive(qDebugLog, &msg, 0) == pdTRUE)
        {
            size_t len = debug_format_line(msg.uptime_ms, msg.text, line, sizeof(line));
            if(!uart3_transmit_dma((uint8_t *)line, (uint16_t)len, UART3_TX_TIMEOUT_MS))
            {
                uart3_panic_write("DebugTask: uart3_transmit_dma failed\r\n");
            }
            dequeued++;
        }

        if(dequeued == DEBUG_QUEUE_LEN)
        {
            size_t len = debug_format_line(HAL_GetTick(), "DebugTask: qDebugLog overflow\r\n", line, sizeof(line));
            if(!uart3_transmit_dma((uint8_t *)line, (uint16_t)len, UART3_TX_TIMEOUT_MS))
            {
                uart3_panic_write("DebugTask: uart3_transmit_dma failed\r\n");
            }
        }
    }
}

/**
  * @brief  Called if vTaskStartScheduler() ever returns (should never
  *         happen); reports it over UART3 and halts
  * @retval None
  */
void scheduler_start_failed(void)
{
    uart3_panic_write("Scheduler failed to start\r\n");
    taskDISABLE_INTERRUPTS();

    for(;;)
    {
    }
}
