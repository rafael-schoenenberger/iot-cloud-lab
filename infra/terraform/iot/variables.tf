variable "aws_region" {
  description = "AWS region for the IoT Core resources."
  type        = string
  default     = "eu-central-1"
}

variable "thing_name" {
  description = "Name of the IoT Thing representing the simulated tracker."
  type        = string
  default     = "iot-cloud-lab-tracker"
}

variable "mqtt_topic" {
  description = "MQTT topic the tracker is allowed to publish telemetry to."
  type        = string
  default     = "trackers/sim1/telemetry"
}
