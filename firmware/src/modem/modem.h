/**
  ******************************************************************************
  * @file    modem.h
  * @author  schoenenberger <rafael@schoenenberger.dev>
  * @date    2026-08-24
  * @brief   SARA-R412M modem task: AT command bring-up, TLS/certificate
  *          provisioning and MQTT publish loop for sensor telemetry.
  ******************************************************************************
  */

#ifndef MODEM_H
#define MODEM_H

void ModemTask(void *argument);

#endif /* MODEM_H */
