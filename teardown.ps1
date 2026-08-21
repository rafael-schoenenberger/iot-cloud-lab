# Stops the simulation stack, then tears down the AWS IoT/DynamoDB
# infrastructure. Intentionally does NOT pass -auto-approve to terraform -
# it will show the destroy plan and ask for an explicit "yes".

docker compose -f "$PSScriptRoot/compose/docker-compose.yml" down

Push-Location "$PSScriptRoot/infra/terraform/iot"
try {
    terraform destroy
}
finally {
    Pop-Location
}
