# ====================================================================================================
# Main File
# ====================================================================================================

# IoT Thing, cert and policy (also writes the device cert/key + root CA to certs_dir)
module "iot_thing" {
  source = "../modules/iot_thing"

  aws_region = var.aws_region
  thing_name = var.thing_name
  mqtt_topic = var.mqtt_topic
  certs_dir  = "${path.module}/certs"
}

# DynamoDB table + IoT Rule that persists telemetry published on mqtt_topic
module "telemetry" {
  source = "../modules/telemetry"

  thing_name     = var.thing_name
  mqtt_topic     = var.mqtt_topic
  certificate_id = module.iot_thing.certificate_id
}
