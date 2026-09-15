"""Periodically drives the simulated TMP108's Temperature property over
Renode's telnet monitor (-P 9005 on the renode service), so the value
drifts over time instead of staying fixed at its .resc startup value.

The drift is a sine wave between MIN_C and MAX_C, covering the full range
every cycle. To avoid a repetitive pattern, the period is re-randomized
within [PERIOD_MIN_S, PERIOD_MAX_S] at each phase wrap (sin() back near
zero), so only the next oscillation's speed changes, never the value itself."""

import math
import random
import socket
import sys
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
CONNECT_MAX_ATTEMPTS = 5
CONNECT_TIMEOUT_S = 10
CONNECT_RETRY_PAUSE_S = 3


def connect():
    """Tries up to CONNECT_MAX_ATTEMPTS times (10s timeout, 3s pause between)
    to reach Renode's monitor, exiting on final failure so `up --wait` can
    still notice a connection that never succeeds."""
    for attempt in range(CONNECT_MAX_ATTEMPTS):
        try:
            sock = socket.create_connection((RENODE_HOST, RENODE_PORT), timeout=CONNECT_TIMEOUT_S)
            print(f"Connected to Renode monitor at {RENODE_HOST}:{RENODE_PORT}", flush=True)
            return sock
        except OSError as exc:
            print(f"Waiting for Renode monitor ({exc}), attempt {attempt + 1}/{CONNECT_MAX_ATTEMPTS}...", flush=True)
            time.sleep(CONNECT_RETRY_PAUSE_S)

    print(f"Could not reach Renode monitor after {CONNECT_MAX_ATTEMPTS} attempts, giving up. "
          "Set it by hand via telnet 127.0.0.1:9005: i2c1.tmp108 Temperature <value>", flush=True)
    sys.exit(1)


def main():
    """Connects, then loops forever: computes the current sine-wave value,
    sends it to Renode every UPDATE_INTERVAL_S, and reconnects (via connect())
    if the socket drops in between."""
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

        while phase >= 2 * math.pi:
            phase -= 2 * math.pi
            period_s = random.uniform(PERIOD_MIN_S, PERIOD_MAX_S)


if __name__ == "__main__":
    main()
