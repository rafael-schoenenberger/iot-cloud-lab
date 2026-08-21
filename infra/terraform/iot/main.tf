terraform {
  required_version = ">= 1.5"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
    http = {
      source  = "hashicorp/http"
      version = "~> 3.0"
    }
    local = {
      source  = "hashicorp/local"
      version = "~> 2.0"
    }
  }
}

provider "aws" {
  region = var.aws_region
}

data "aws_caller_identity" "current" {}

data "aws_iot_endpoint" "current" {
  endpoint_type = "iot:Data-ATS"
}

# Amazon's root CA, needed by the modem/client to verify AWS IoT Core's server
# certificate. Fetched once at apply time so the whole cert bundle is produced
# by a single `terraform apply`, no manual download step.
# Tradeoff (future todo): every plan/apply now depends on this URL being
# reachable - could be avoided by vendoring the CA statically instead.
data "http" "amazon_root_ca" {
  url = "https://www.amazontrust.com/repository/AmazonRootCA1.pem"
}

resource "aws_iot_thing" "tracker" {
  name = var.thing_name
}

resource "aws_iot_certificate" "cert" {
  active = true
}

resource "aws_iot_thing_principal_attachment" "attach" {
  thing     = aws_iot_thing.tracker.name
  principal = aws_iot_certificate.cert.arn
}

resource "aws_iot_policy" "policy" {
  name = "${var.thing_name}-policy"

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = "iot:Connect"
        Resource = "arn:aws:iot:${var.aws_region}:${data.aws_caller_identity.current.account_id}:client/${var.thing_name}"
      },
      {
        Effect   = "Allow"
        Action   = "iot:Publish"
        Resource = "arn:aws:iot:${var.aws_region}:${data.aws_caller_identity.current.account_id}:topic/${var.mqtt_topic}"
      }
    ]
  })
}

resource "aws_iot_policy_attachment" "attach" {
  policy = aws_iot_policy.policy.name
  target = aws_iot_certificate.cert.arn
}

resource "local_file" "device_cert" {
  content         = aws_iot_certificate.cert.certificate_pem
  filename        = "${path.module}/certs/device.pem.crt"
  file_permission = "0600"
}

resource "local_file" "device_key" {
  content         = aws_iot_certificate.cert.private_key
  filename        = "${path.module}/certs/device.pem.key"
  file_permission = "0600"
}

resource "local_file" "amazon_root_ca" {
  content         = data.http.amazon_root_ca.response_body
  filename        = "${path.module}/certs/AmazonRootCA1.pem"
  file_permission = "0600"
}
