/**
  ******************************************************************************
  * @file    modem.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-18
  * @brief   AT command helpers for the SARA-R412M modem (send/wait, blocking
  *          line reads, cert upload) and ModemTask (network/TLS/MQTT bring-up
  *          + publish loop; payload building in modem_payload.c/.h)
  ******************************************************************************
  */

#include "modem.h"
#include "uart.h"
#include "sensor.h"
#include "modem_payload.h"
#include "aws_certs_key_params.h"
#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Covers at_send()/at_send_cert() */
#define UART1_TX_TIMEOUT_MS 1000

/* at_send_retry(): retry count before giving up */
#define AT_SEND_MAX_RETRIES 3

/* at_wait_for()'s per-line receive buffer */
#define AT_LINE_BUF_LEN 128

/* at_send_cert(): its AT+USECMNG command buffer and the '>' prompt/final-OK
 * timeouts either side of the raw cert/key data transfer */
#define AT_SEND_CERT_CMD_BUF_LEN       96
#define AT_SEND_CERT_PROMPT_TIMEOUT_MS 2000
#define AT_SEND_CERT_OK_TIMEOUT_MS     5000

/* at_send_cert()'s `type` argument; AT+USECMNG certificate type */
#define AT_SEND_CERT_TYPE_CA    0
#define AT_SEND_CERT_TYPE_CERT  1
#define AT_SEND_CERT_TYPE_KEY   2

/* Placeholder; a real SIM/carrier would define the actual APN string */
#define AWS_APN "iot"

/* ModemTask(): command buffer, timeouts, and the bring-up retry delay */
#define AT_CMD_BUF_LEN              256
#define AT_WAIT_OK_TIMEOUT_MS       2000
#define AT_MQTT_CONNECT_TIMEOUT_MS  30000
#define AT_PUBLISH_OK_TIMEOUT_MS    5000
#define AT_BRINGUP_RETRY_DELAY_MS   5000

/**
  * @brief  Sends an AT command (with trailing CR/LF) on UART1 via DMA
  * @param  cmd NUL-terminated command string
  * @retval true if the transfer was started, false on timeout/HAL error
  */
static bool at_send(const char *cmd)
{
    return uart1_transmit_dma((const uint8_t *)cmd, (uint16_t)strlen(cmd), UART1_TX_TIMEOUT_MS);
}

/**
  * @brief  Calls at_send(), retrying up to AT_SEND_MAX_RETRIES times before
  *         giving up. Prints a debug message on final failure
  * @param  cmd NUL-terminated command string
  * @retval true if at_send() succeeded on any attempt, false if it failed
  *         every time
  */
static bool at_send_retry(const char *cmd)
{
    for(int attempt = 0; attempt < AT_SEND_MAX_RETRIES; attempt++)
    {
        if(at_send(cmd))
        {
            return true;
        }
    }

    DBG("at_send failed after %d attempts: %s", AT_SEND_MAX_RETRIES, cmd)

    return false;
}

/**
  * @brief  Reads '\r'/'\n'-terminated lines (see uart1_read_line()) until
  *         one contains `needle`, an "ERROR" line arrives, or the timeout
  *         expires; logs discarded lines and ERROR via DBG()
  * @param  needle Substring to look for in each received line (e.g. "OK",
  *         "+UUMQTTC: 1,0"). Must not contain '\r' or '\n'; uart1_read_line()
  *         strips those as line terminators, so they can never appear in a
  *         line to match against
  * @param  timeout_ms Maximum time to wait, in milliseconds
  * @retval true if a line containing `needle` was received, false on
  *         "ERROR" or timeout
  */
static bool at_wait_for(const char *needle, uint32_t timeout_ms)
{
    char line[AT_LINE_BUF_LEN];

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while((xTaskGetTickCount() - start) < timeout_ticks)
    {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if(!uart1_read_line(line, sizeof(line), pdTICKS_TO_MS(timeout_ticks - elapsed)))
        {
            return false;
        }

        if(strstr(line, needle) != NULL)
        {
            return true;
        }

        if(strcmp(line, "ERROR") == 0)
        {
            DBG("ERROR (want '%s')", needle)
            return false;
        }

        DBG("discarded '%s' (want '%s')", line, needle)
    }

    return false;
}

/**
  * @brief  Shorthand for at_wait_for("OK", timeout_ms)
  * @param  timeout_ms Maximum time to wait, in milliseconds
  * @retval true if "OK" was received, false on "ERROR" or timeout
  */
static bool at_wait_ok(uint32_t timeout_ms)
{
    return at_wait_for("OK", timeout_ms);
}

/**
  * @brief  Streams a certificate/key over UART1 via AT+USECMNG=0,... (section
  *         20.3.2 of the SARA-R4/N4 AT Commands Manual, R22): sends the
  *         command, waits for the '>' prompt, then pushes the raw bytes
  * @param  type USECMNG certificate type (AT_SEND_CERT_TYPE_CA/_CERT/_KEY)
  * @param  internal_name Name the modem should store the object under
  * @param  data Raw certificate/key bytes
  * @param  data_len Length of `data` in bytes
  * @retval true on success, false if at_send_retry() failed, the '>' prompt
  *         or final OK never arrived, or the raw data transfer itself failed
  */
static bool at_send_cert(int type, const char *internal_name, const char *data, size_t data_len)
{
    char cmd[AT_SEND_CERT_CMD_BUF_LEN];

    snprintf(cmd, sizeof(cmd), "AT+USECMNG=0,%d,\"%s\",%u\r\n",
             type, internal_name, (unsigned)data_len);

    if(!at_send_retry(cmd))
    {
        DBG("at_send_cert: failed to send USECMNG for '%s'", internal_name)
        return false;
    }

    if(!uart1_wait_char('>', AT_SEND_CERT_PROMPT_TIMEOUT_MS))
    {
        DBG("at_send_cert: '>' prompt never arrived for '%s'", internal_name)
        return false;
    }

    if(!uart1_transmit_dma((const uint8_t *)data, (uint16_t)data_len, UART1_TX_TIMEOUT_MS))
    {
        DBG("at_send_cert: raw data transfer failed for '%s'", internal_name)
        return false;
    }

    if(!at_wait_ok(AT_SEND_CERT_OK_TIMEOUT_MS))
    {
        DBG("at_send_cert: final OK never arrived for '%s'", internal_name)
        return false;
    }

    return true;
}

/**
  * @brief  Brings up the SARA-R412M modem (network attach, certificate
  *         upload, TLS profile, MQTT connect), then loops publishing every
  *         SensorSample_t received from qSensorData to AWS IoT until a
  *         publish fails, at which point the whole bring-up is redone. Every
  *         command is echoed back before its "OK" (ATE1, the factory
  *         default); only the "OK" is actually checked, the echo is just
  *         another discarded line as far as at_wait_for() is concerned
  * @param  argument Unused
  * @retval None
  */
void ModemTask(void *argument)
{
    (void)argument;
    char cmd[AT_CMD_BUF_LEN];

    for(;;)
    {
        /* liveness check: is the modem responding on UART1 at all? */
        if(!at_send_retry("AT\r\n") || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }

        /* define PDP context 1: IP APN for the cellular connection (13.4) */
        snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", AWS_APN);
        if(!at_send_retry(cmd) || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }

        /* attach to the packet-switched network (13.6) */
        if(!at_send_retry("AT+CGATT=1\r\n") || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }

        /* activate PDP context 1 (13.7) */
        if(!at_send_retry("AT+CGACT=1,1\r\n") || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }

        /* upload the CA/client cert/client key used for the TLS handshake (20.3.2) */
        if(!at_send_cert(AT_SEND_CERT_TYPE_CA, "AWS-CA", AWS_ROOT_CA, AWS_ROOT_CA_LEN) ||
           !at_send_cert(AT_SEND_CERT_TYPE_CERT, "AWS-CERT", AWS_DEVICE_CERT, AWS_DEVICE_CERT_LEN) ||
           !at_send_cert(AT_SEND_CERT_TYPE_KEY, "AWS-KEY", AWS_DEVICE_KEY, AWS_DEVICE_KEY_LEN))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }

        /* TLS profile 0 (20.3.3): validate the server cert against our imported CA */
        if(!at_send_retry("AT+USECPRF=0,0,1\r\n") || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }
        /* trusted root CA's internal name */
        if(!at_send_retry("AT+USECPRF=0,3,\"AWS-CA\"\r\n") || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }
        /* client certificate's internal name */
        if(!at_send_retry("AT+USECPRF=0,5,\"AWS-CERT\"\r\n") || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }
        /* client private key's internal name */
        if(!at_send_retry("AT+USECPRF=0,6,\"AWS-KEY\"\r\n") || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }

        /* MQTT profile (28.2): our unique client ID */
        snprintf(cmd, sizeof(cmd), "AT+UMQTT=0,\"%s\"\r\n", AWS_IOT_CLIENT_ID);
        if(!at_send_retry(cmd) || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }

        /* AWS IoT broker hostname and port */
        snprintf(cmd, sizeof(cmd), "AT+UMQTT=2,\"%s\",8883\r\n", AWS_IOT_ENDPOINT);
        if(!at_send_retry(cmd) || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }

        /* TLS on, USECMNG profile 0 */
        if(!at_send_retry("AT+UMQTT=11,1,0\r\n") || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }
        /* clean session */
        if(!at_send_retry("AT+UMQTT=12,1\r\n") || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }

        /* MQTT login/connect (28.6); immediate ack that the request was accepted */
        if(!at_send_retry("AT+UMQTTC=1\r\n") || !at_wait_ok(AT_WAIT_OK_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }
        /* async login result (28.6): 0 means the broker accepted the connection */
        if(!at_wait_for("+UUMQTTC: 1,0", AT_MQTT_CONNECT_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(AT_BRINGUP_RETRY_DELAY_MS));
            continue;
        }

        for(;;)
        {
            SensorSample_t sample;
            xQueueReceive(qSensorData, &sample, portMAX_DELAY);

            /* publish this sample to AWS_MQTT_TOPIC (28.6) */
            modem_build_publish_cmd(sample, cmd, sizeof(cmd));
            if(!at_send_retry(cmd) || !at_wait_ok(AT_PUBLISH_OK_TIMEOUT_MS))
            {
                break;
            }
        }
    }
}
