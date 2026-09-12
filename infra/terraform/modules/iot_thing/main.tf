# ====================================================================================================
# IoT Thing, certificate and policy - also writes the device cert/key + root CA to certs_dir
# ====================================================================================================

# Account ID used to scope the IoT policy's resource ARNs
data "aws_caller_identity" "current" {}

# MQTT endpoint (Data-ATS) the device connects to
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

  lifecycle {
    # SHA256 pinned 2026-09-12 (curl + sha256sum) - same "pin + trip-wire"
    # pattern as the Docker base image digests and Doxygen/Renode tarball
    # checksums elsewhere in this project. Catches transit corruption and
    # flags any future unexpected change to the file at this URL.
    postcondition {
      condition     = sha256(self.response_body) == "2c43952ee9e000ff2acc4e2ed0897c0a72ad5fa72c3d934e81741cbd54f05bd1"
      error_message = "Amazon Root CA 1 content changed since this hash was pinned (2026-09-12) - verify the new content/hash independently before updating this value."
    }
  }
}

# The IoT Thing representing the simulated tracker
resource "aws_iot_thing" "tracker" {
  name = var.thing_name
}

# Device certificate used to authenticate the tracker
resource "aws_iot_certificate" "cert" {
  active = true
}

# Attaches the certificate to the IoT Thing
resource "aws_iot_thing_principal_attachment" "attach" {
  thing     = aws_iot_thing.tracker.name
  principal = aws_iot_certificate.cert.arn
}

# Policy allowing the device to connect and publish telemetry
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

# Attaches the policy to the certificate
resource "aws_iot_policy_attachment" "attach" {
  policy = aws_iot_policy.policy.name
  target = aws_iot_certificate.cert.arn
}

# file_permission below is a no-op on Windows (no POSIX permission bits) -
# harmless, but don't rely on it as real protection for the private key on
# this dev machine; it does take effect on a Linux/macOS host.
resource "local_file" "device_cert" {
  content         = aws_iot_certificate.cert.certificate_pem
  filename        = "${var.certs_dir}/device.pem.crt"
  file_permission = "0600"
}

# Writes the device's private key to certs_dir
resource "local_file" "device_key" {
  content         = aws_iot_certificate.cert.private_key
  filename        = "${var.certs_dir}/device.pem.key"
  file_permission = "0600"
}

# Writes the Amazon root CA to certs_dir
resource "local_file" "amazon_root_ca" {
  content         = data.http.amazon_root_ca.response_body
  filename        = "${var.certs_dir}/AmazonRootCA1.pem"
  file_permission = "0600"
}
