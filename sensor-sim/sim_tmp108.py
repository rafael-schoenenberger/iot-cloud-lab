"""Periodically drives the simulated TMP108's Temperature property over
Renode's telnet monitor (-P 9005 on the renode service), so the sensor value
drifts over time instead of staying fixed at its .resc startup value.

The drift is a sine wave between MIN_C and MAX_C, guaranteeing the full
range is visibly covered every cycle. To avoid a perfectly repetitive,
predictable pattern, the period is re-randomized within
[PERIOD_MIN_S, PERIOD_MAX_S] every time a cycle completes (i.e. only at a
phase wrap, where sin() is back near zero, so the temperature itself never
jumps - only the speed of the next oscillation changes)."""

import math
import random
import socket
import time

RENODE_HOST = "renode"
RENODE_PORT = 9005
MIN_C = 15
MAX_C = 30
BASELINE_C = (MIN_C + MAX_C) / 2
AMPLITUDE_C = (MAX_C - MIN_C) / 2
PERIOD_MIN_S = 40
PERIOD_MAX_S = 120
UPDATE_INTERVAL_S = 5


def connect():
    while True:
        try:
            sock = socket.create_connection((RENODE_HOST, RENODE_PORT), timeout=10)
            print(f"Connected to Renode monitor at {RENODE_HOST}:{RENODE_PORT}", flush=True)
            return sock
        except OSError as exc:
            print(f"Waiting for Renode monitor ({exc}), retrying in 3s...", flush=True)
            time.sleep(3)


def main():
    sock = connect()
    phase = 0.0
    period_s = random.uniform(PERIOD_MIN_S, PERIOD_MAX_S)

    while True:
        temp_c = BASELINE_C + AMPLITUDE_C * math.sin(phase)

        try:
            sock.sendall(f"i2c1.tmp108 Temperature {round(temp_c)}\n".encode())
            print(f"Set TMP108 Temperature = {round(temp_c)} (period={period_s:.0f}s)", flush=True)
        except OSError as exc:
            print(f"Lost connection to Renode monitor ({exc}), reconnecting...", flush=True)
            sock.close()
            sock = connect()
            continue

        time.sleep(UPDATE_INTERVAL_S)

        phase += 2 * math.pi * UPDATE_INTERVAL_S / period_s
        if phase >= 2 * math.pi:
            phase -= 2 * math.pi
            period_s = random.uniform(PERIOD_MIN_S, PERIOD_MAX_S)


if __name__ == "__main__":
    main()
