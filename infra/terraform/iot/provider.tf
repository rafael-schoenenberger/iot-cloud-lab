# ====================================================================================================
# Terraform settings and provider configuration
# ====================================================================================================

# Defines the required provider, and minimum versions
terraform {
  required_version = ">= 1.6"

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

  # Recommended to save state files on bucket, needs to be created prior to deployment:
  #
  #   aws s3api create-bucket \
  #     --bucket tf-state-schoenenberger-iot-cloud-lab \
  #     --region eu-central-1 \
  #     --create-bucket-configuration LocationConstraint=eu-central-1
  #
  #   backend "s3" {
  #     bucket = "tf-state-schoenenberger-iot-cloud-lab"
  #     key    = "iot/terraform.tfstate"
  #     region = var.aws_region
  #   }
}

# Configures AWS with the default region
provider "aws" {
  region = var.aws_region
}
