/**
  ******************************************************************************
  * @file    debug.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-15
  * @brief   Dedicated debug-output task: DebugTask alone drains qDebugLog and
  *          prints each message on UART3, so other tasks log via the DBG()
  *          macro instead of calling uart3_transmit_dma() themselves
  ******************************************************************************
  */

#ifndef DEBUG_H
#define DEBUG_H

#include "FreeRTOS.h"
#include "queue.h"

/* 1 = DBG() enqueues onto qDebugLog as usual, 0 = DBG() compiles to nothing.
 * Override with -DDEBUG=1 to enable debug output */
#ifndef DEBUG
#define DEBUG 0
#endif

#define DEBUG_MSG_LEN   64
#define DEBUG_QUEUE_LEN 10

typedef struct
{
    char text[DEBUG_MSG_LEN];
    uint16_t len;
    uint32_t uptime_ms; /**< HAL_GetTick() at the moment DBG() was called */
} DebugMsg_t;

extern QueueHandle_t qDebugLog;

void debug_log(const char *fmt, ...);
void DebugTask(void *argument);

/* Call sites use this instead of debug_log() directly. The trailing ';'
 * is baked in on purpose, so call sites write DBG(...) with no semicolon */
#if DEBUG
#define DBG(...) debug_log(__VA_ARGS__);
#else
#define DBG(...) do {} while(0);
#endif

#endif /* DEBUG_H */
