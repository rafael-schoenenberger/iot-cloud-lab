# builds firmware, then builds/starts renode (which loads it), temp-sim and
# modem-sim - rerun anytime after changes
# once it's up, connect to the UART3 debug output yourself: putty -raw 127.0.0.1 9002

docker compose -f "$PSScriptRoot/compose/docker-compose.yml" up --build
