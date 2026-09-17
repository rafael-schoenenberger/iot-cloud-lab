/**
  ******************************************************************************
  * @file    test_modem.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-17
  * @brief   Host-side Unity tests for modem_payload.c (hex_encode and
  *          modem_build_publish_cmd); no FreeRTOS/HAL/hardware involved.
  *          "Filling the queue" is simulated by constructing a SensorSample_t
  *          directly and passing it in, standing in for what TempTask would
  *          have pushed onto qSensorData
  ******************************************************************************
  */

#include "unity.h"
#include "modem_payload.h"

/**
  * @brief  Unity per-test setup hook. Nothing to do; modem_payload.c has no
  *         state to reset between tests
  * @retval None
  */
void setUp(void) {}

/**
  * @brief  Unity per-test teardown hook. Nothing to do, see setUp()
  * @retval None
  */
void tearDown(void) {}

/* Shared by test_hex_encode_basic() and test_hex_encode_empty_input() below */
#define HEX_OUT_BUF_LEN 8

#define HEX_ENCODE_BASIC_INPUT    "AB"
#define HEX_ENCODE_BASIC_EXPECTED "4142"

/**
  * @brief  hex_encode() on a plain two-byte input
  * @retval None
  */
static void test_hex_encode_basic(void)
{
    char out[HEX_OUT_BUF_LEN];

    hex_encode(HEX_ENCODE_BASIC_INPUT, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(HEX_ENCODE_BASIC_EXPECTED, out);
}

/**
  * @brief  hex_encode() on an empty string produces an empty string
  * @retval None
  */
static void test_hex_encode_empty_input(void)
{
    char out[HEX_OUT_BUF_LEN];

    hex_encode("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

#define HEX_ENCODE_TRUNCATE_INPUT    "ABCDEF"
#define HEX_ENCODE_TRUNCATE_EXPECTED "41"

/**
  * @brief  hex_encode() truncates instead of overflowing `out` when
  *         `out_size` is too small for the full input. out_size=3 leaves
  *         room for exactly one byte's hex pair + NUL; the 2nd input
  *         byte's pair (i*2+2 == 4) no longer satisfies "< out_size", so it
  *         and everything after it is dropped
  * @retval None
  */
static void test_hex_encode_truncates_to_fit_out_size(void)
{
    char out[3];

    hex_encode(HEX_ENCODE_TRUNCATE_INPUT, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(HEX_ENCODE_TRUNCATE_EXPECTED, out);
}

#define HEX_ENCODE_ZERO_SIZE_SENTINEL 'X'

/**
  * @brief  hex_encode() with out_size=0 must not write anything to `out`
  *         (regression test for a former 1-byte out-of-bounds write)
  * @retval None
  */
static void test_hex_encode_zero_out_size_is_a_no_op(void)
{
    char out[1] = { HEX_ENCODE_ZERO_SIZE_SENTINEL };

    hex_encode(HEX_ENCODE_BASIC_INPUT, out, 0);
    TEST_ASSERT_EQUAL_CHAR(HEX_ENCODE_ZERO_SIZE_SENTINEL, out[0]);
}

/* Shared by both modem_build_publish_cmd() tests below */
#define CMD_BUF_LEN 256

#define KNOWN_GOOD_TEMP_C    22
#define KNOWN_GOOD_UPTIME_MS 2000

/**
  * @brief  modem_build_publish_cmd() against a known-good sample. Expected
  *         output cross-checked against modem-sim's actual log output for
  *         the same {temp_c, uptime_ms} pair during a live run against AWS
  *         IoT (see AT+UMQTTC=2,... in docker compose logs modem-sim)
  * @retval None
  */
static void test_modem_build_publish_cmd_matches_known_good_output(void)
{
    SensorSample_t sample = { .temp_c = KNOWN_GOOD_TEMP_C, .uptime_ms = KNOWN_GOOD_UPTIME_MS };
    char cmd[CMD_BUF_LEN];

    modem_build_publish_cmd(sample, cmd, sizeof(cmd));

    TEST_ASSERT_EQUAL_STRING(
        "AT+UMQTTC=2,0,0,1,\"trackers/sim1/telemetry\","
        "\"7b2274656d705f63223a32322c22757074696d655f6d73223a323030307d\"\r\n",
        cmd);
}

#define NEGATIVE_TEMP_C         -5
#define NEGATIVE_TEMP_UPTIME_MS 123456

/**
  * @brief  modem_build_publish_cmd() with a negative temp_c (int8_t, so
  *         negative values are valid input); checks the JSON/hex-encoding
  *         handles the sign correctly
  * @retval None
  */
static void test_modem_build_publish_cmd_handles_negative_temp(void)
{
    SensorSample_t sample = { .temp_c = NEGATIVE_TEMP_C, .uptime_ms = NEGATIVE_TEMP_UPTIME_MS };
    char cmd[CMD_BUF_LEN];

    modem_build_publish_cmd(sample, cmd, sizeof(cmd));

    /* {"temp_c":-5,"uptime_ms":123456} hex-encoded byte by byte */
    TEST_ASSERT_EQUAL_STRING(
        "AT+UMQTTC=2,0,0,1,\"trackers/sim1/telemetry\","
        "\"7b2274656d705f63223a2d352c22757074696d655f6d73223a3132333435367d\"\r\n",
        cmd);
}

#define CMD_TRUNCATE_BUF_LEN 10
#define CMD_TRUNCATE_EXPECTED "AT+UMQTTC"

/**
  * @brief  modem_build_publish_cmd() truncates instead of overflowing `cmd`
  *         when `cmd_size` is too small: it builds the command via
  *         snprintf() (see modem_payload.c), which never writes past
  *         `cmd_size` and always NUL-terminates
  * @retval None
  */
static void test_modem_build_publish_cmd_truncates_to_fit_cmd_size(void)
{
    SensorSample_t sample = { .temp_c = KNOWN_GOOD_TEMP_C, .uptime_ms = KNOWN_GOOD_UPTIME_MS };
    char cmd[CMD_TRUNCATE_BUF_LEN];

    modem_build_publish_cmd(sample, cmd, sizeof(cmd));
    TEST_ASSERT_EQUAL_STRING(CMD_TRUNCATE_EXPECTED, cmd);
}

/**
  * @brief  Test runner entry point: registers and runs all test_*() cases
  * @retval int Unity's aggregate result (0 if all tests passed)
  */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_hex_encode_basic);
    RUN_TEST(test_hex_encode_empty_input);
    RUN_TEST(test_hex_encode_truncates_to_fit_out_size);
    RUN_TEST(test_hex_encode_zero_out_size_is_a_no_op);
    RUN_TEST(test_modem_build_publish_cmd_matches_known_good_output);
    RUN_TEST(test_modem_build_publish_cmd_handles_negative_temp);
    RUN_TEST(test_modem_build_publish_cmd_truncates_to_fit_cmd_size);

    return UNITY_END();
}
