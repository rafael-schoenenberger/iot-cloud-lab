# ====================================================================================================
# telemetry output values
# ====================================================================================================

# DynamoDB table the IoT Rule persists incoming telemetry to
output "telemetry_table" {
  description = "DynamoDB table the IoT Rule persists incoming telemetry to."
  value       = aws_dynamodb_table.telemetry.name
}
