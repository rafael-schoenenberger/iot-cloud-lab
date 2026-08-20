# builds firmware, then builds/starts renode which loads it - rerun anytime after changes
# once it's up, connect to the UART3 debug output yourself: putty -raw 127.0.0.1 9002

docker compose -f ./compose/docker-compose.yml up --build
