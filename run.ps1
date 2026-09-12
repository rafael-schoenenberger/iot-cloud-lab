<#
.SYNOPSIS
    Deploys the AWS IoT/DynamoDB infra, runs quality checks, then builds and
    starts the simulation stack.
.DESCRIPTION
    Runs terraform init/plan/apply against infra/terraform/iot, regenerates
    firmware/src/aws_certs.h, runs docs/lint/unit-test (Doxygen, cppcheck,
    Unity), then builds/force-recreates temp-sim/modem-sim first (picks up
    an edited .py source before firmware boots), then firmware-build/renode.
    Opens PuTTY on the UART3 debug output once healthy; safe to rerun anytime.
.NOTES
    Author: schoenenberger <rafael@schoenenberger.dev>
    Date:   2026-09-12
#>

# Terraform module directory - Deploy-Infra cd's into it via Push-Location
$terraformDir = "$PSScriptRoot/infra/terraform/iot"

# Compose file passed to every "docker compose -f $compose ..." call below
$compose = "$PSScriptRoot/compose/docker-compose.yml"

# Read by docker-compose.yml's build.args - real build time for every
# image's org.opencontainers.image.created label, never a stale hardcoded date
$env:BUILD_DATE = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")

# Applies the Terraform module, then regenerates aws_certs.h from its output
# so the firmware embeds the freshly (re-)issued device cert
function Deploy-Infra {
    Push-Location $terraformDir
    try {
        # No catch anywhere in this script - each check below aborts the
        # whole run.ps1 right here (via exit 1), before any container
        # gets built/started

        # -input=false: never block on an interactive prompt
        terraform init -input=false
        if ($LASTEXITCODE -ne 0) {
            Write-Host "terraform init failed" -ForegroundColor Red
            exit 1
        }

        # -out=tfplan: save the computed plan to a file instead of just
        # printing it, so apply below runs exactly this plan
        terraform plan -out=tfplan
        if ($LASTEXITCODE -ne 0) {
            Write-Host "terraform plan failed" -ForegroundColor Red
            exit 1
        }

        # Applying a saved plan file runs it as-is: no re-evaluation, no
        # confirmation prompt of its own
        terraform apply tfplan
        if ($LASTEXITCODE -ne 0) {
            Write-Host "terraform apply failed" -ForegroundColor Red
            exit 1
        }
    }
    finally {
        # Always runs, even if exit 1 above fired: deletes the
        # plan file (ignoring the error if it was never created) and
        # restores the working directory Push-Location changed above
        Remove-Item -Path tfplan -ErrorAction SilentlyContinue
        Pop-Location
    }

    # Keeps aws_certs.h in sync with the deployed cert; harmless to rerun
    # even when nothing changed (only the @date stamp updates).
    python "$terraformDir/generate_firmware_certs.py"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "generate_firmware_certs.py failed" -ForegroundColor Red
        exit 1
    }
}

# Runs Doxygen, cppcheck and the Unity unit tests, in that order
function Invoke-QualityCheck {
    # Purely visual - marks the start of this section in the console output
    Write-Host "Running docs/lint/unit-test..." -ForegroundColor Cyan

    # docs/lint are informational only (no --error-exitcode for cppcheck) -
    # shown, never abort. --build so a Dockerfile.sources change is picked
    # up now, before Start-Stack's own --build runs.
    docker compose -f $compose --profile docs run --build --rm docs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "docs step failed to run (non-fatal, continuing)." -ForegroundColor Yellow
    }

    docker compose -f $compose --profile lint run --build --rm lint
    if ($LASTEXITCODE -ne 0) {
        Write-Host "lint step failed to run (non-fatal, continuing)." -ForegroundColor Yellow
    }

    # unit-test's exit code does reflect real pass/fail (Unity + pipefail in
    # the service's own command) - broken firmware logic should stop here,
    # before spending time on the AWS/MQTT stack below.
    docker compose -f $compose --profile unit-test run --build --rm unit-test
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Unit tests failed." -ForegroundColor Red
        exit 1
    }
}

# Builds firmware/renode/temp-sim/modem-sim and waits for a healthy MQTT
# connection before returning.
function Start-Stack {
    # temp-sim/modem-sim bind-mount their .py source (no COPY) and must be
    # force-recreated BEFORE the firmware boots - ModemTask connects/uploads
    # certs only once (modem.c), so a later replacement breaks MQTT forever
    # (found via real testing). --no-deps: both retry their own connection
    # until renode is reachable; no --wait since modem-sim can't be healthy yet.
    docker compose -f $compose up -d --build --force-recreate --no-deps temp-sim modem-sim
    if ($LASTEXITCODE -ne 0) {
        Write-Host "temp-sim/modem-sim failed to start - showing logs below." -ForegroundColor Red
        docker compose -f $compose logs temp-sim modem-sim --tail=200
        exit 1
    }

    # Now build/start firmware-build + renode - the firmware boots fresh
    # against the already-current temp-sim/modem-sim above, so its one-time
    # bring-up talks to the right instances. --wait covers all 4 services,
    # including modem-sim's real MQTT-session HEALTHCHECK (catches a stale
    # device cert instead of silently leaving DynamoDB empty).
    docker compose -f $compose up -d --build --wait
    if ($LASTEXITCODE -ne 0) {
        # Any of the 4 services could be the cause here (only modem-sim has
        # a HEALTHCHECK, but renode/firmware-build/temp-sim failing to reach
        # "running"/"exited(0)" fails --wait too) - show all of them.
        Write-Host "Stack did not become healthy - showing logs from all services below." -ForegroundColor Red
        docker compose -f $compose logs --tail=200
        exit 1
    }

    Write-Host "Stack healthy - telemetry should be flowing into DynamoDB." -ForegroundColor Green
}

# Opens PuTTY on the UART3 debug console, unless something is already
# connected to it - checked via the TCP connection itself (127.0.0.1:9002,
# Established), not just "is any putty.exe running".
function Start-DebugConsole {
    $alreadyConnected = Get-NetTCPConnection -RemoteAddress 127.0.0.1 -RemotePort 9002 -State Established -ErrorAction SilentlyContinue
    if (-not $alreadyConnected) {
        Start-Process putty -ArgumentList "-raw", "127.0.0.1", "9002"
    }
}

# Follows every service's logs - never returns, Ctrl+C to stop.
function Show-Log {
    docker compose -f $compose logs -f
}

# Calls the functions defined above, in correct order
Deploy-Infra
Invoke-QualityCheck
Start-Stack
Start-DebugConsole
Show-Log
