/**
  ******************************************************************************
  * @file    modem_payload.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-25
  * @brief   Pure (no FreeRTOS/HAL dependency) helpers for building the
  *          AT+UMQTTC publish command from a SensorSample_t - split out of
  *          modem.c so they can be unit-tested on the host, see
  *          firmware/unit_tests/modem/test_modem.c.
  ******************************************************************************
  */

#ifndef MODEM_PAYLOAD_H
#define MODEM_PAYLOAD_H

#include "sensor_types.h"
#include <stddef.h>

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
void hex_encode(const char *in, char *out, size_t out_size);

/**
  * @brief  Builds the full AT+UMQTTC=2,... publish command (topic plus
  *         hex-encoded payload, CR/LF-terminated) for one sample: formats
  *         it as JSON, then hex-encodes the JSON via hex_encode().
  * @param  sample Sample to publish.
  * @param  cmd Buffer receiving the NUL-terminated AT command.
  * @param  cmd_size Size of `cmd` in bytes.
  * @retval None
  */
void modem_build_publish_cmd(SensorSample_t sample, char *cmd, size_t cmd_size);

#endif /* MODEM_PAYLOAD_H */
