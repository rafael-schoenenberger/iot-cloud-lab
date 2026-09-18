/**
  ******************************************************************************
  * @file    test_debug.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-18
  * @brief   Host-side Unity tests for debug_format.c (debug_format_msg and
  *          debug_format_line); no FreeRTOS/HAL/hardware involved
  ******************************************************************************
  */

#include "unity.h"
#include "debug_format.h"
#include <stdarg.h>
#include <string.h>

/**
  * @brief  Unity per-test setup hook. Nothing to do; debug_format.c has no
  *         state to reset between tests
  * @retval None
  */
void setUp(void) {}

/**
  * @brief  Unity per-test teardown hook. Nothing to do, see setUp()
  * @retval None
  */
void tearDown(void) {}

/**
  * @brief  Test-only wrapper: builds a va_list from `...` so tests can call
  *         debug_format_msg() the same way debug_log() does
  * @retval Same as debug_format_msg()
  */
static size_t call_debug_format_msg(char *out, size_t out_size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    size_t len = debug_format_msg(out, out_size, fmt, args);
    va_end(args);
    return len;
}

#define MSG_OUT_BUF_LEN 32

/**
  * @brief  debug_format_msg() on a plain printf-style format
  * @retval None
  */
static void test_debug_format_msg_basic(void)
{
    char out[MSG_OUT_BUF_LEN];

    size_t len = call_debug_format_msg(out, sizeof(out), "temp=%d", 22);

    TEST_ASSERT_EQUAL_STRING("temp=22", out);
    TEST_ASSERT_EQUAL_UINT(7, len);
}

#define LINE_OUT_BUF_LEN 32

/**
  * @brief  debug_format_line() on a normal uptime_ms/text pair, including
  *         the trailing "\r\n" it appends
  * @retval None
  */
static void test_debug_format_line_basic(void)
{
    char out[LINE_OUT_BUF_LEN];

    size_t len = debug_format_line(1234, "hello", out, sizeof(out));

    TEST_ASSERT_EQUAL_STRING("[1234] hello\r\n", out);
    TEST_ASSERT_EQUAL_UINT(14, len);
}

/**
  * @brief  debug_format_line() truncates instead of overflowing `out` when
  *         `out_size` is too small for the full "[<uptime_ms>] <text>\r\n" line
  * @retval None
  */
static void test_debug_format_line_truncates_to_fit_out_size(void)
{
    char out[8];

    size_t len = debug_format_line(1234, "hello world", out, sizeof(out));

    TEST_ASSERT_EQUAL_STRING("[1234] ", out);
    TEST_ASSERT_EQUAL_UINT(7, len);
}

/**
  * @brief  Test runner entry point: registers and runs all test_*() cases
  * @retval int Unity's aggregate result (0 if all tests passed)
  */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_debug_format_msg_basic);
    RUN_TEST(test_debug_format_line_basic);
    RUN_TEST(test_debug_format_line_truncates_to_fit_out_size);

    return UNITY_END();
}
