/**
  ******************************************************************************
  * @file    modem.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   AT command helpers for the SARA-R412M modem (send/wait, blocking
  *          line reads, certificate upload, hex-encoding for MQTT publish)
  *          and ModemTask, which drives network attach, TLS/certificate
  *          provisioning, MQTT connect and the publish loop.
  ******************************************************************************
  */

#include "modem.h"
#include "uart.h"
#include "sensor.h"
#include "aws_certs.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Placeholder - a real SIM/carrier would define the actual APN string. */
#define AWS_APN "iot"

/**
  * @brief  Sends a raw AT command string on UART1.
  * @param  cmd NUL-terminated command string, including any trailing CR/LF.
  * @retval None
  */
static void at_send(const char *cmd)
{
    HAL_UART_Transmit(&huart1, (const uint8_t *)cmd, (uint16_t)strlen(cmd), HAL_MAX_DELAY);
}

/**
  * @brief  Reads lines until one contains `needle` (success) or an "ERROR"
  *         line or the timeout arrives first (failure). Some responses (e.g.
  *         AT+USECMNG's "+USECMNG: ...\r\nOK\r\n") span an info line before
  *         the final result code, so a single uart1_read_line() call is not
  *         always enough.
  * @param  needle Substring to look for in each received line.
  * @param  timeout_ms Maximum time to wait, in milliseconds.
  * @retval true if a line containing `needle` was received, false on
  *         "ERROR" or timeout.
  */
static bool at_wait_for(const char *needle, uint32_t timeout_ms)
{
    char line[128];
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (!uart1_read_line(line, sizeof(line), pdTICKS_TO_MS(timeout_ticks - elapsed))) {
            return false;
        }
        if (strstr(line, needle) != NULL) {
            return true;
        }
        if (strcmp(line, "ERROR") == 0) {
            return false;
        }
    }
    return false;
}

/**
  * @brief  Shorthand for at_wait_for("OK", timeout_ms).
  * @param  timeout_ms Maximum time to wait, in milliseconds.
  * @retval true if "OK" was received, false on "ERROR" or timeout.
  */
static bool at_wait_ok(uint32_t timeout_ms)
{
    return at_wait_for("OK", timeout_ms);
}

/**
  * @brief  Streams a certificate/key over UART1 via AT+USECMNG=0,... (section
  *         19.2 of the SARA-R4/N4 AT Commands Manual): sends the command,
  *         waits for the '>' prompt, then pushes the raw bytes.
  * @param  type USECMNG certificate type (0 = CA, 1 = client cert, 2 = key).
  * @param  internal_name Name the modem should store the object under.
  * @param  data Raw certificate/key bytes.
  * @param  data_len Length of `data` in bytes.
  * @retval true on success, false if the '>' prompt or the final OK never
  *         arrived.
  */
static bool at_send_cert(int type, const char *internal_name, const char *data, size_t data_len)
{
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "AT+USECMNG=0,%d,\"%s\",%u\r\n",
             type, internal_name, (unsigned)data_len);
    at_send(cmd);

    if (!uart1_wait_char('>', 2000)) {
        return false;
    }

    HAL_UART_Transmit(&huart1, (const uint8_t *)data, (uint16_t)data_len, HAL_MAX_DELAY);
    return at_wait_ok(5000);
}

/**
  * @brief  Hex-encodes `in` into `out`. AT+UMQTTC's publish command takes the
  *         message as a quoted string parameter, but our payloads are JSON
  *         containing literal '"' characters, which a quoted-parameter AT
  *         parser cannot tell apart from the closing quote. Hex-encoding
  *         (hex_mode=1, see 24.5.3) sidesteps that entirely.
  * @param  in NUL-terminated input string.
  * @param  out Buffer receiving the NUL-terminated hex string.
  * @param  out_size Size of `out` in bytes.
  * @retval None
  */
static void hex_encode(const char *in, char *out, size_t out_size)
{
    static const char hex_chars[] = "0123456789abcdef";
    size_t i = 0;
    for (; in[i] != '\0' && (i * 2 + 2) < out_size; i++) {
        out[i * 2] = hex_chars[((uint8_t)in[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex_chars[(uint8_t)in[i] & 0xF];
    }
    out[i * 2] = '\0';
}

/**
  * @brief  Brings up the SARA-R412M modem (network attach, certificate
  *         upload, TLS profile, MQTT connect), then loops forever publishing
  *         every SensorSample_t received from qSensorData to AWS IoT.
  * @param  argument Unused.
  * @retval None
  */
void ModemTask(void *argument)
{
    (void)argument;
    char cmd[256];

    at_send("AT\r\n");
    at_wait_ok(2000);

    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", AWS_APN);
    at_send(cmd);
    at_wait_ok(2000);

    at_send("AT+CGATT=1\r\n");
    at_wait_ok(2000);

    at_send("AT+CGACT=1,1\r\n");
    at_wait_ok(2000);

    at_send_cert(0, "AWS-CA", AWS_ROOT_CA, AWS_ROOT_CA_LEN);
    at_send_cert(1, "AWS-CERT", AWS_DEVICE_CERT, AWS_DEVICE_CERT_LEN);
    at_send_cert(2, "AWS-KEY", AWS_DEVICE_KEY, AWS_DEVICE_KEY_LEN);

    /* TLS profile 0: validate the server cert against our imported CA. */
    at_send("AT+USECPRF=0,0,1\r\n");
    at_wait_ok(2000);
    at_send("AT+USECPRF=0,3,\"AWS-CA\"\r\n");
    at_wait_ok(2000);
    at_send("AT+USECPRF=0,5,\"AWS-CERT\"\r\n");
    at_wait_ok(2000);
    at_send("AT+USECPRF=0,6,\"AWS-KEY\"\r\n");
    at_wait_ok(2000);

    snprintf(cmd, sizeof(cmd), "AT+UMQTT=0,\"%s\"\r\n", AWS_IOT_CLIENT_ID);
    at_send(cmd);
    at_wait_ok(2000);

    snprintf(cmd, sizeof(cmd), "AT+UMQTT=2,\"%s\",8883\r\n", AWS_IOT_ENDPOINT);
    at_send(cmd);
    at_wait_ok(2000);

    at_send("AT+UMQTT=11,1,0\r\n"); /* TLS on, USECMNG profile 0 */
    at_wait_ok(2000);
    at_send("AT+UMQTT=12,1\r\n"); /* clean session */
    at_wait_ok(2000);

    at_send("AT+UMQTTC=1\r\n");
    at_wait_ok(2000);                    /* immediate ack that the request was accepted */
    at_wait_for("+UUMQTTC: 1,0", 30000); /* async connect result (24.5.4) */

    for (;;) {
        SensorSample_t sample;
        xQueueReceive(qSensorData, &sample, portMAX_DELAY);

        char json[48];
        snprintf(json, sizeof(json), "{\"temp_c\":%d,\"uptime_ms\":%lu}",
                 sample.temp_c, (unsigned long)sample.uptime_ms);
        char hex[96];
        hex_encode(json, hex, sizeof(hex));

        snprintf(cmd, sizeof(cmd), "AT+UMQTTC=2,0,0,1,\"%s\",\"%s\"\r\n", AWS_MQTT_TOPIC, hex);
        at_send(cmd);
        at_wait_ok(5000);
    }
}
