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

void uart3_init(void);

void uart1_init(void);

bool uart1_wait_char(char expected, uint32_t timeout_ms);

bool uart1_read_line(char *line, size_t line_size, uint32_t timeout_ms);

bool uart1_transmit_dma(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

bool uart3_transmit_dma(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

#endif /* UART_H */
