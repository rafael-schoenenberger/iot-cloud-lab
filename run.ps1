# Check Renode image
if (-not (docker image inspect renode:latest | Out-Null)) {
    Write-Host "[INFO] Renode image not found — building..."
    docker build -t renode:latest -f ../renode/Dockerfile.renode ../renode
} else {
    Write-Host "[INFO] Renode image already exists — skipping build."
}

# Check Firmware image
if (-not (docker image inspect firmware:latest | Out-Null)) {
    Write-Host "[INFO] Firmware image not found — building..."
    docker build -t firmware:latest -f ../firmware/Dockerfile.sources ../firmware
} else {
    Write-Host "[INFO] Firmware image already exists — skipping build."
}

# Start compose from compose/ folder
Write-Host "[INFO] Starting docker compose..."
docker compose -f ./compose/docker-compose.yml up
