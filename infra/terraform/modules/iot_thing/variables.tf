# ====================================================================================================
# iot_thing variables
# ====================================================================================================

# AWS region the IoT policy's resource ARNs are scoped to
variable "aws_region" {
  description = "AWS region the IoT policy's resource ARNs are scoped to."
  type        = string
}

# Name of the IoT Thing representing the simulated tracker
variable "thing_name" {
  description = "Name of the IoT Thing representing the simulated tracker."
  type        = string
}

# MQTT topic the tracker is allowed to publish telemetry to
variable "mqtt_topic" {
  description = "MQTT topic the tracker is allowed to publish telemetry to."
  type        = string
}

# Directory the device cert, private key and Amazon root CA are written to
variable "certs_dir" {
  description = "Directory the device cert, private key and Amazon root CA are written to."
  type        = string
}
