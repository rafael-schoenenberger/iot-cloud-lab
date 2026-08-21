# Persists incoming telemetry (see main.tf's aws_iot_policy, which is what
# actually allows the device to publish on var.mqtt_topic in the first
# place) so it can be browsed later instead of only live via the MQTT test
# client - MQTT itself has no storage, a message is gone the instant it's
# delivered (or dropped, if nobody is subscribed).

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

resource "aws_iam_role" "iot_rule" {
  name = "${var.thing_name}-iot-rule-role"

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
}
