# ====================================================================================================
# Infrastructure output values
# ====================================================================================================

# MQTT endpoint the modem connects to
output "iot_endpoint" {
  description = "AWS IoT Core MQTT endpoint (Data-ATS) the modem connects to."
  value       = module.iot_thing.iot_endpoint
}

# Name of the IoT Thing / MQTT client ID
output "thing_name" {
  description = "Name of the IoT Thing / MQTT client ID."
  value       = module.iot_thing.thing_name
}

# Topic the tracker publishes telemetry to
output "mqtt_topic" {
  description = "MQTT topic the tracker publishes telemetry to."
  value       = var.mqtt_topic
}

# Local directory holding the device cert, key and root CA
output "certs_dir" {
  description = "Local directory containing device.pem.crt, device.pem.key and AmazonRootCA1.pem."
  value       = "${path.module}/certs"
}

# DynamoDB table the IoT Rule writes telemetry to
output "telemetry_table" {
  description = "DynamoDB table the IoT Rule persists incoming telemetry to."
  value       = module.telemetry.telemetry_table
}

# AWS region the IoT/DynamoDB resources live in
output "aws_region" {
  description = "AWS region the IoT/DynamoDB resources live in."
  value       = var.aws_region
}
