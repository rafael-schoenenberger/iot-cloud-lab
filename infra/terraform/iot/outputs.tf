output "iot_endpoint" {
  description = "AWS IoT Core MQTT endpoint (Data-ATS) the modem connects to."
  value       = data.aws_iot_endpoint.current.endpoint_address
}

output "thing_name" {
  description = "Name of the IoT Thing / MQTT client ID."
  value       = aws_iot_thing.tracker.name
}

output "mqtt_topic" {
  description = "MQTT topic the tracker publishes telemetry to."
  value       = var.mqtt_topic
}

output "certs_dir" {
  description = "Local directory containing device.pem.crt, device.pem.key and AmazonRootCA1.pem."
  value       = "${path.module}/certs"
}

output "telemetry_table" {
  description = "DynamoDB table the IoT Rule persists incoming telemetry to."
  value       = aws_dynamodb_table.telemetry.name
}

output "aws_region" {
  description = "AWS region the IoT/DynamoDB resources live in."
  value       = var.aws_region
}
