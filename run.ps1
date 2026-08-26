# deploys the AWS IoT/DynamoDB infra, runs docs/lint/unit-test (Doxygen,
# cppcheck, Unity), then builds firmware and builds/starts renode (which
# loads it), temp-sim and modem-sim - rerun anytime after changes
# opens PuTTY on the UART3 debug output (127.0.0.1:9002) once it's up

$terraformDir = "$PSScriptRoot/infra/terraform/iot"
$compose = "$PSScriptRoot/compose/docker-compose.yml"

function Deploy-Infra {
    Push-Location $terraformDir
    try {
        terraform init -input=false
        if ($LASTEXITCODE -ne 0) { throw "terraform init failed" }
        terraform plan -out=tfplan
        if ($LASTEXITCODE -ne 0) { throw "terraform plan failed" }
        terraform apply tfplan
        if ($LASTEXITCODE -ne 0) { throw "terraform apply failed" }
    }
    finally {
        Remove-Item -Path tfplan -ErrorAction SilentlyContinue
        Pop-Location
    }

    # Keeps the firmware's embedded cert (aws_certs.h) in sync whenever apply
    # re-issues the device cert. Cheap no-op otherwise - safe to always run.
    python "$terraformDir/generate_firmware_certs.py"
    if ($LASTEXITCODE -ne 0) { throw "generate_firmware_certs.py failed" }
}

function Run-QualityChecks {
    Write-Host "Running docs/lint/unit-test..." -ForegroundColor Cyan

    # docs/lint are informational only (no --error-exitcode configured for
    # cppcheck) - shown, but never abort the run.
    docker compose -f $compose --profile docs run --rm docs
    docker compose -f $compose --profile lint run --rm lint

    # unit-test's exit code does reflect real pass/fail (Unity + pipefail in
    # the service's own command) - broken firmware logic should stop here,
    # before spending time on the AWS/MQTT stack below.
    docker compose -f $compose --profile unit-test run --rm unit-test
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Unit tests failed - see output above." -ForegroundColor Red
        exit 1
    }
}

Deploy-Infra
Run-QualityChecks

# --wait fails fast unless modem-sim's HEALTHCHECK reports a real MQTT
# session with AWS IoT - catches a stale device cert instead of silently
# leaving DynamoDB empty.
docker compose -f $compose up -d --build --wait
if ($LASTEXITCODE -ne 0) {
    Write-Host "Stack did not become healthy - check modem-sim's log below." -ForegroundColor Red
    docker compose -f $compose logs modem-sim --tail=40
    exit 1
}
Write-Host "Stack healthy - telemetry should be flowing into DynamoDB." -ForegroundColor Green

if (-not (Get-Process putty -ErrorAction SilentlyContinue)) {
    Start-Process putty -ArgumentList "-raw", "127.0.0.1", "9002"
}

docker compose -f $compose logs -f
