/**
  ******************************************************************************
  * @file    modem_payload.c
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-25
  * @brief   Pure (no FreeRTOS/HAL dependency) helpers for building the
  *          AT+UMQTTC publish command from a SensorSample_t
  ******************************************************************************
  */

#include "modem_payload.h"
#include "aws_certs.h"
#include <stdint.h>
#include <stdio.h>

/**
  * @brief  Hex-encodes `in` into `out`. AT+UMQTTC's publish command takes the
  *         message as a quoted string parameter, but our payloads are JSON
  *         containing literal '"' characters, which a quoted-parameter AT
  *         parser cannot tell apart from the closing quote. Hex-encoding
  *         (hex_mode=1, see 24.5.3) sidesteps that entirely
  * @param  in NUL-terminated input string
  * @param  out Buffer receiving the NUL-terminated hex string
  * @param  out_size Size of `out` in bytes
  * @retval None
  */
void hex_encode(const char *in, char *out, size_t out_size)
{
    static const char hex_chars[] = "0123456789abcdef";
    size_t i = 0;

    for(; in[i] != '\0' && (i*2 + 2) < out_size; i++)
    {
        out[i*2] = hex_chars[((uint8_t)in[i] >> 4) & 0xF];
        out[i*2+1] = hex_chars[(uint8_t)in[i] & 0xF];
    }

    out[i*2] = '\0';
}

/**
  * @brief  Builds the full AT+UMQTTC=2,... publish command (topic plus
  *         hex-encoded payload, CR/LF-terminated) for one sample: formats
  *         it as JSON, then hex-encodes the JSON via hex_encode()
  * @param  sample Sample to publish
  * @param  cmd Buffer receiving the NUL-terminated AT command
  * @param  cmd_size Size of `cmd` in bytes
  * @retval None
  */
void modem_build_publish_cmd(SensorSample_t sample, char *cmd, size_t cmd_size)
{
    char json[48];

    snprintf(json, sizeof(json), "{\"temp_c\":%d,\"uptime_ms\":%lu}",
             sample.temp_c, (unsigned long)sample.uptime_ms);

    char hex[96];

    hex_encode(json, hex, sizeof(hex));
    snprintf(cmd, cmd_size, "AT+UMQTTC=2,0,0,1,\"%s\",\"%s\"\r\n", AWS_MQTT_TOPIC, hex);
}
