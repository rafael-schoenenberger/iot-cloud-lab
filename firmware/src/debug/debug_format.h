/**
  ******************************************************************************
  * @file    debug_format.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-11
  * @brief   Pure (no FreeRTOS/HAL dependency) message-formatting helpers for
  *          debug.c - split out so they can be unit-tested on the host, see
  *          firmware/unit_tests/debug/test_debug.c
  ******************************************************************************
  */

#ifndef DEBUG_FORMAT_H
#define DEBUG_FORMAT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

size_t debug_format_msg(char *out, size_t out_size, const char *fmt, va_list args);
size_t debug_format_line(uint32_t uptime_ms, const char *text, char *out, size_t out_size);

#endif /* DEBUG_FORMAT_H */
