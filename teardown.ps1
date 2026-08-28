# Stops the simulation stack, then tears down the AWS IoT/DynamoDB
# infrastructure. Intentionally does NOT pass -auto-approve to terraform -
# it will show the destroy plan and ask for an explicit "yes".

$compose = "$PSScriptRoot/compose/docker-compose.yml"

docker compose -f $compose down --remove-orphans

# `down` can't see one-off `docker compose run` containers (docs/lint/
# unit-test, if one got left running by an interrupted run) - sweep separately.
$stray = docker compose -f $compose --profile docs --profile lint --profile unit-test ps -aq
if ($stray) {
    docker rm -f $stray
}

Push-Location "$PSScriptRoot/infra/terraform/iot"
try {
    terraform destroy
}
finally {
    Pop-Location
}
