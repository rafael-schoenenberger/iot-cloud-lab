/**
  ******************************************************************************
  * @file    debug_format.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-18
  * @brief   Pure (no FreeRTOS/HAL dependency) message-formatting helpers for
  *          debug.c
  ******************************************************************************
  */

#include "debug_format.h"
#include <stdio.h>

/**
  * @brief  Formats `fmt`/`args` into `out`, clamping to `out_size` instead of
  *         overflowing it
  * @param  out Buffer receiving the NUL-terminated formatted text
  * @param  out_size Size of `out` in bytes
  * @param  fmt printf-style format string
  * @param  args Already-started va_list for `fmt`
  * @retval Length of the text written to `out`, excluding the NUL terminator
  */
size_t debug_format_msg(char *out, size_t out_size, const char *fmt, va_list args)
{
    int len = vsnprintf(out, out_size, fmt, args);

    if(len > (int)out_size - 1)
    {
        len = (int)out_size - 1; /* vsnprintf() returns the untruncated length */
    }

    return (size_t)len;
}

/**
  * @brief  Formats one debug line as "[<uptime_ms>] <text>\r\n", clamping to
  *         `out_size` instead of overflowing it
  * @param  uptime_ms Timestamp to prefix the line with
  * @param  text NUL-terminated message text, without a trailing "\r\n"
  * @param  out Buffer receiving the NUL-terminated formatted line
  * @param  out_size Size of `out` in bytes
  * @retval Length of the line written to `out`, excluding the NUL terminator
  */
size_t debug_format_line(uint32_t uptime_ms, const char *text, char *out, size_t out_size)
{
    int len = snprintf(out, out_size, "[%lu] %s\r\n", (unsigned long)uptime_ms, text);

    if(len > (int)out_size - 1)
    {
        len = (int)out_size - 1; /* snprintf() returns the untruncated length */
    }

    return (size_t)len;
}
