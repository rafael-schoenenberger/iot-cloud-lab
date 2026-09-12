<#
.SYNOPSIS
    Stops the simulation stack and tears down the AWS IoT/DynamoDB infra.
.DESCRIPTION
    Runs `docker compose down`, sweeps any stray one-off containers left
    behind by an interrupted docs/lint/unit-test run, then runs
    `terraform destroy -auto-approve` against infra/terraform/iot - no
    interactive confirmation, destroys the AWS resources immediately.
.NOTES
    Author: schoenenberger <rafael@schoenenberger.dev>
    Date:   2026-09-12
#>

$compose = "$PSScriptRoot/compose/docker-compose.yml"
$terraformDir = "$PSScriptRoot/infra/terraform/iot"

# Scoped to this project (compose/ folder name), stops firmware-build/
# renode/temp-sim/modem-sim. Doesn't exit on failure - a docker compose
# failure shouldn't block the cost-relevant terraform destroy below.
docker compose -f $compose down --remove-orphans
if ($LASTEXITCODE -ne 0) {
    Write-Host "docker compose down failed - containers still running:" -ForegroundColor Red
    docker compose -f $compose ps
} else {
    Write-Host "Stack stopped." -ForegroundColor Green
}

# `down` above already handled the 4 default-profile services (firmware-
# build/renode/temp-sim/modem-sim) - it can't see one-off `docker compose
# run` containers (docs/lint/unit-test) though, so sweep those separately.
$stray = docker compose -f $compose --profile docs --profile lint --profile unit-test ps -aq
if ($stray) {
    Write-Host "Removing stray docs/lint/unit-test containers." -ForegroundColor Yellow
    docker rm -f $stray
} else {
    Write-Host "No stray docs/lint/unit-test containers found." -ForegroundColor Green
}

# Destroys all AWS resources (IoT Thing/cert/policy, DynamoDB table, IAM
# role, topic rule) with no confirmation prompt. finally restores the
# working directory even if destroy itself fails.
Push-Location $terraformDir
try {
    terraform destroy -auto-approve
}
finally {
    Pop-Location
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "terraform destroy failed - AWS resources may still be running." -ForegroundColor Red
} else {
    Write-Host "AWS resources destroyed." -ForegroundColor Green
}
