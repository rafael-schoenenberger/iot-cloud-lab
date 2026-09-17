/**
  ******************************************************************************
  * @file    modem_payload.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-09-17
  * @brief   Pure (no FreeRTOS/HAL dependency) helpers for building the
  *          AT+UMQTTC publish command from a SensorSample_t; split out of
  *          modem.c so they can be unit-tested on the host, see
  *          firmware/unit_tests/modem/test_modem.c
  ******************************************************************************
  */

#ifndef MODEM_PAYLOAD_H
#define MODEM_PAYLOAD_H

#include "sensor_types.h"
#include <stddef.h>

void hex_encode(const char *in, char *out, size_t out_size);
void modem_build_publish_cmd(SensorSample_t sample, char *cmd, size_t cmd_size);

#endif /* MODEM_PAYLOAD_H */
