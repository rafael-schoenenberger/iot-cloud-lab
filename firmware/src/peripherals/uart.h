/**
  ******************************************************************************
  * @file    uart.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   USART3 (debug console) and USART1 (SARA-R412M modem link)
  *          handles, initialization and blocking helpers for reading AT
  *          command responses out of the UART1 RX stream buffer.
  ******************************************************************************
  */

#ifndef UART_H
#define UART_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart1;

/**
  * @brief  Initializes USART3 (115200 8N1) used as the debug console.
  * @retval None
  */
void uart3_init(void);

/**
  * @brief  Initializes USART1 (115200 8N1) used for the SARA-R412M modem
  *         link, and arms interrupt-driven single-byte reception into the
  *         internal RX stream buffer consumed by uart1_wait_char() and
  *         uart1_read_line().
  * @retval None
  */
void uart1_init(void);

/**
  * @brief  Blocks (no polling) until `expected` arrives on UART1 or the
  *         timeout elapses. Used only for AT+USECMNG's '>' prompt, which -
  *         unlike every other modem response - is not itself CR/LF-terminated.
  * @param  expected Character to wait for.
  * @param  timeout_ms Maximum time to wait, in milliseconds.
  * @retval true if `expected` was received, false on timeout.
  */
bool uart1_wait_char(char expected, uint32_t timeout_ms);

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
bool uart1_read_line(char *line, size_t line_size, uint32_t timeout_ms);

#endif /* UART_H */
