# ====================================================================================================
# telemetry variables
# ====================================================================================================

# Name of the IoT Thing whose telemetry is being persisted
variable "thing_name" {
  description = "Name of the IoT Thing whose telemetry is being persisted."
  type        = string
}

# MQTT topic the IoT Rule subscribes to
variable "mqtt_topic" {
  description = "MQTT topic the IoT Rule subscribes to."
  type        = string
}

# ID of the device certificate (module.iot_thing.certificate_id), used as an IAM role name suffix
variable "certificate_id" {
  description = "ID of the device certificate (module.iot_thing.certificate_id), used as an IAM role name suffix."
  type        = string
}
