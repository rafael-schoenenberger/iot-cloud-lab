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

/**
  * @brief  Brings up the SARA-R412M modem (network attach, certificate
  *         upload, TLS profile, MQTT connect), then loops forever publishing
  *         every SensorSample_t received from qSensorData to AWS IoT.
  * @param  argument Unused.
  * @retval None
  */
void ModemTask(void *argument);

#endif /* MODEM_H */
