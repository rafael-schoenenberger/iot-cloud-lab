# ====================================================================================================
# Telemetry persistence - DynamoDB table + IoT Rule that stores published MQTT messages
# ====================================================================================================

# Account ID used in the IAM role's trust policy
data "aws_caller_identity" "current" {}

# Table telemetry messages are written to
resource "aws_dynamodb_table" "telemetry" {
  name         = "${var.thing_name}-telemetry"
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "client_id"
  range_key    = "ts"

  attribute {
    name = "client_id"
    type = "S"
  }

  attribute {
    name = "ts"
    type = "N"
  }
}

# Role the IoT Rule assumes to write to DynamoDB/CloudWatch
resource "aws_iam_role" "iot_rule" {
  # Cert-ID suffix so a destroy+recreate gets a fresh ARN - AWS IoT Core
  # cached stale STS tokens for a reused role name for up to ~10 minutes.
  name = "${var.thing_name}-iot-rule-role-${substr(var.certificate_id, 0, 8)}"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect    = "Allow"
      Principal = { Service = "iot.amazonaws.com" }
      Action    = "sts:AssumeRole"
      Condition = {
        StringEquals = {
          "aws:SourceAccount" = data.aws_caller_identity.current.account_id
        }
      }
    }]
  })
}

# Grants the role permission to write telemetry items
resource "aws_iam_role_policy" "iot_rule_dynamodb" {
  name = "${var.thing_name}-iot-rule-dynamodb"
  role = aws_iam_role.iot_rule.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect   = "Allow"
      Action   = "dynamodb:PutItem"
      Resource = aws_dynamodb_table.telemetry.arn
    }]
  })
}

# Diagnostic: the dynamodbv2 action fails silently otherwise - this is the
# only way to see *why* a PutItem was rejected.
resource "aws_cloudwatch_log_group" "iot_rule_errors" {
  name              = "/aws/iot/${var.thing_name}-telemetry-errors"
  retention_in_days = 3
}

# Grants the role permission to write error logs
resource "aws_iam_role_policy" "iot_rule_logs" {
  name = "${var.thing_name}-iot-rule-logs"
  role = aws_iam_role.iot_rule.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect   = "Allow"
      Action   = ["logs:CreateLogStream", "logs:PutLogEvents"]
      Resource = "${aws_cloudwatch_log_group.iot_rule_errors.arn}:*"
    }]
  })
}

# Future todo: if the payload ever gains its own "client_id"/"ts" field,
# it could collide with the ones added below by the SELECT clause.
resource "aws_iot_topic_rule" "telemetry_to_dynamodb" {
  name        = replace("${var.thing_name}_telemetry_to_dynamodb", "-", "_")
  enabled     = true
  sql         = "SELECT *, clientid() AS client_id, timestamp() AS ts FROM '${var.mqtt_topic}'"
  sql_version = "2016-03-23"

  dynamodbv2 {
    role_arn = aws_iam_role.iot_rule.arn

    put_item {
      table_name = aws_dynamodb_table.telemetry.name
    }
  }

  error_action {
    cloudwatch_logs {
      log_group_name = aws_cloudwatch_log_group.iot_rule_errors.name
      role_arn       = aws_iam_role.iot_rule.arn
    }
  }

  # Only aws_iam_role.iot_rule is referenced above (via role_arn), not these
  # two policies directly - without this, Terraform infers no dependency on
  # them and could create the rule before IAM finishes propagating the
  # permissions it actually needs at publish time.
  depends_on = [aws_iam_role_policy.iot_rule_dynamodb, aws_iam_role_policy.iot_rule_logs]
}
