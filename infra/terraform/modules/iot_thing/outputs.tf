# ====================================================================================================
# iot_thing output values
# ====================================================================================================

# Name of the IoT Thing / MQTT client ID
output "thing_name" {
  description = "Name of the IoT Thing / MQTT client ID."
  value       = aws_iot_thing.tracker.name
}

# ID of the device certificate, used elsewhere to derive resource names that must change on cert rotation
output "certificate_id" {
  description = "ID of the device certificate, used elsewhere to derive resource names that must change on cert rotation."
  value       = aws_iot_certificate.cert.id
}

# AWS IoT Core MQTT endpoint (Data-ATS) the modem connects to
output "iot_endpoint" {
  description = "AWS IoT Core MQTT endpoint (Data-ATS) the modem connects to."
  value       = data.aws_iot_endpoint.current.endpoint_address
}
