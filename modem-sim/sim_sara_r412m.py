"""Simulates a u-blox SARA-R412M cellular modem's AT command interface over
Renode's UART1 TCP relay (see renode/scripts/stm32f4.resc). Implements just
enough of the official AT command set for the firmware's ModemTask to drive
a real MQTT/TLS connection to AWS IoT Core; see the u-blox SARA-R4/N4 AT
Commands Manual (UBX-17003787-R10):
  - Section 10.20 (Command echo E): every command line is echoed back before
    its result code, matching the factory-programmed ATE1 default; the
    firmware never sends ATE0, so this is unconditional here.
  - Section 13 (Packet switched data services): AT+CGDCONT/+CGATT/+CGACT are
    accepted and always answered OK. There is no real cellular network here
    to attach to; the firmware still sends them (matching real hardware,
    where the Cat-M1 SARA-R412M auto-attaches and auto-activates its PDP
    context, so the host only needs to define the APN and can otherwise
    treat this as a formality) but this simulator has nothing to do beyond
    acknowledging them.
  - Section 19 (SSL/TLS) in R10, section 20.3.2 in the newer R22 revision:
    AT+USECMNG imports the CA/client cert/client key, streamed as raw bytes
    after a '>' prompt exactly like the real modem.
  - Section 24 (MQTT) in R10, section 28 in the newer R22 revision:
    AT+UMQTT configures the modem's built-in MQTT client, AT+UMQTTC triggers
    connect/publish. The modem (real or simulated) owns the whole TLS+MQTT
    stack; the firmware never sees a raw socket.

The certificates received via AT+USECMNG are what this simulator actually
uses to open a *real* TLS connection to AWS IoT Core via paho-mqtt; the AT
layer is not decorative, it is the only path the cert bytes travel.

Publish payloads are always sent hex-encoded (AT+UMQTTC=2,<QoS>,<retain>,1,
<topic>,<hex message>, hex_mode=1 per 28.6) rather than as a quoted ASCII
string: our payloads are JSON and contain literal '"' characters, which a
naive quoted-string AT parameter parser cannot distinguish from the closing
quote of the <message> parameter. Hex has no such ambiguity, no quotes, no
commas, which is exactly the documented purpose of that parameter."""

import hashlib
import os
import pathlib
import socket
import ssl
import tempfile
import threading
import time

import paho.mqtt.client as mqtt

RENODE_HOST = "renode"
RENODE_PORT = 9006

CERT_TYPE_NAMES = {0: "CA", 1: "CC", 2: "PK"}

# Checked by the Dockerfile's HEALTHCHECK; present only while a real MQTT
# session with AWS IoT is up (see Modem._set_healthy).
HEALTH_FILE = pathlib.Path("/tmp/mqtt_healthy")


def parse_params(body):
    """Split an AT command's comma-separated parameter string into tokens,
    respecting double-quoted strings (which may contain commas/spaces)."""
    params = []
    i, n = 0, len(body)
    while i < n:
        if body[i] == '"':
            j = body.index('"', i + 1)
            params.append(body[i + 1:j])
            i = j + 1
            if i < n and body[i] == ",":
                i += 1
        else:
            j = body.find(",", i)
            if j == -1:
                j = n
            params.append(body[i:j])
            i = j + 1
    return params


class LineReader:
    """Reads AT command lines (terminated by \\r and/or \\n) or a fixed
    number of raw bytes (for AT+USECMNG's binary cert upload) off one
    socket, buffering whatever was over-read for the next call."""

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def _fill(self):
        chunk = self.sock.recv(4096)
        if not chunk:
            raise ConnectionError("UART1 socket closed")
        self.buf += chunk

    def read_exact(self, count):
        while len(self.buf) < count:
            self._fill()
        data, self.buf = self.buf[:count], self.buf[count:]
        return data

    def read_line(self):
        while b"\r" not in self.buf and b"\n" not in self.buf:
            self._fill()
        idx_candidates = [i for i in (self.buf.find(b"\r"), self.buf.find(b"\n")) if i != -1]
        idx = min(idx_candidates)
        # A \r\n terminator can arrive split across two TCP reads; wait for
        # a second byte before deciding whether it's a 1- or 2-byte
        # terminator, or a stray leftover byte corrupts the next read_exact().
        while len(self.buf) < idx + 2:
            self._fill()
        line = self.buf[:idx]
        rest = self.buf[idx:idx + 2]
        skip = 2 if rest in (b"\r\n", b"\n\r") else 1
        self.buf = self.buf[idx + skip:]
        return line.decode(errors="replace")


class Modem:
    def __init__(self, sock):
        self.sock = sock
        self.reader = LineReader(sock)
        self.certs = {}  # cert type (0=CA/1=client cert/2=client key) -> raw bytes from USECMNG
        self.mqtt_client_id = ""
        self.mqtt_server = None
        self.mqtt_port = 8883
        self.mqtt_client = None
        # Guards send(): both the main thread and paho's network thread write
        # to the same socket, and concurrent sendall()s could interleave.
        self.send_lock = threading.Lock()
        self._set_healthy(False)

    @staticmethod
    def _set_healthy(healthy):
        try:
            if healthy:
                HEALTH_FILE.touch()
            else:
                HEALTH_FILE.unlink(missing_ok=True)
        except OSError:
            pass  # advisory only; never let this take the modem down

    def send(self, text):
        with self.send_lock:
            self.sock.sendall(text.encode())

    def reply_ok(self, info=None):
        if info:
            self.send(f"\r\n{info}\r\n")
        self.send("\r\nOK\r\n")

    def reply_error(self):
        self.send("\r\nERROR\r\n")

    def run(self):
        while True:
            line = self.reader.read_line().strip()
            if not line:
                continue
            print(f"< {line}", flush=True)
            # Command echo E (10.20): factory default is ATE1 (echo on), and
            # the firmware never sends ATE0, so every command line is echoed
            # back before its result code, like the real modem would
            self.send(f"{line}\r\n")
            try:
                self.handle(line)
            except (ValueError, IndexError, KeyError) as exc:
                # Malformed/unexpected AT parameters; stay up and answer
                # ERROR like a real modem would, instead of taking the whole
                # process down (there's no restart: policy on this container).
                print(f"Malformed AT command '{line}': {exc}", flush=True)
                self.reply_error()

    def handle(self, line):
        if not line.upper().startswith("AT"):
            self.reply_error()
            return
        body = line[2:]
        if body == "":
            self.reply_ok()
            return
        if not body.startswith("+"):
            self.reply_ok()  # permissive default for basic V.25ter commands
            return

        body = body[1:]
        if "=" in body:
            name, rest = body.split("=", 1)
        elif body.endswith("?"):
            name, rest = body[:-1], "?"
        else:
            name, rest = body, ""
        name = name.upper()

        if name in ("CGDCONT", "CGATT", "CGACT", "CFUN", "CEREG", "CREG"):
            self.reply_ok()
        elif name == "USECMNG":
            self.handle_usecmng(rest)
        elif name == "USECPRF":
            self.reply_ok()
        elif name == "UMQTT":
            self.handle_umqtt(rest)
        elif name == "UMQTTC":
            self.handle_umqttc(rest)
        else:
            self.reply_ok()  # be permissive about anything else the firmware sends

    def handle_usecmng(self, rest):
        params = parse_params(rest)
        op_code, cert_type, internal_name, data_size = int(params[0]), int(params[1]), params[2], int(params[3])
        if op_code != 0:
            self.reply_error()
            return
        self.send("\r\n>")
        data = self.reader.read_exact(data_size)
        self.certs[cert_type] = data
        md5 = hashlib.md5(data).hexdigest()
        type_name = CERT_TYPE_NAMES.get(cert_type, "?")
        print(f"Stored {type_name} cert '{internal_name}' ({data_size} bytes, md5 {md5})", flush=True)
        self.reply_ok(f'+USECMNG: 0,{cert_type},"{internal_name}","{md5}"')

    def handle_umqtt(self, rest):
        params = parse_params(rest)
        op_code = int(params[0])
        if op_code == 0:
            self.mqtt_client_id = params[1]
        elif op_code == 2:
            self.mqtt_server = params[1]
            if len(params) > 2 and params[2] != "":
                self.mqtt_port = int(params[2])
        self.reply_ok(f"+UMQTT: {op_code},1")

    def handle_umqttc(self, rest):
        params = parse_params(rest)
        op_code = int(params[0])
        if op_code == 1:
            self.reply_ok("+UMQTTC: 1,1")
            self.mqtt_connect()
        elif op_code == 2:
            qos, retain, hex_mode, topic, message = (
                int(params[1]), int(params[2]), int(params[3]), params[4], params[5],
            )
            if hex_mode:
                message = bytes.fromhex(message).decode("utf-8", errors="replace")
            self.mqtt_publish(topic, message, qos, retain)
            self.reply_ok("+UMQTTC: 2,1")
        elif op_code == 0:
            self.mqtt_disconnect()
            self.reply_ok("+UMQTTC: 0,1")
        else:
            self.reply_error()

    def mqtt_connect(self):
        ca_path = cert_path = key_path = None
        try:
            ca_bytes, cert_bytes, key_bytes = self.certs[0], self.certs[1], self.certs[2]
            with tempfile.NamedTemporaryFile(suffix=".pem", delete=False) as ca_f:
                ca_f.write(ca_bytes)
                ca_path = ca_f.name
            with tempfile.NamedTemporaryFile(suffix=".pem", delete=False) as cert_f:
                cert_f.write(cert_bytes)
                cert_path = cert_f.name
            with tempfile.NamedTemporaryFile(suffix=".pem", delete=False) as key_f:
                key_f.write(key_bytes)
                key_path = key_f.name

            client = mqtt.Client(
                callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
                client_id=self.mqtt_client_id,
            )
            client.on_connect = self._on_mqtt_connect
            client.on_disconnect = self._on_mqtt_disconnect
            client.tls_set(
                ca_certs=ca_path,
                certfile=cert_path,
                keyfile=key_path,
                tls_version=ssl.PROTOCOL_TLS_CLIENT,
            )
            client.connect(self.mqtt_server, self.mqtt_port)
            self.mqtt_client = client
            client.loop_start()
            print(f"MQTT connecting to {self.mqtt_server}:{self.mqtt_port} as '{self.mqtt_client_id}'...", flush=True)
        except Exception as exc:
            print(f"MQTT connect failed: {exc}", flush=True)
            self.mqtt_client = None
            self.send("\r\n+UUMQTTC: 1,3\r\n")
        finally:
            for path in (ca_path, cert_path, key_path):
                if path:
                    os.unlink(path)

    def _on_mqtt_connect(self, client, userdata, flags, reason_code, properties):
        # Fires once the broker acks (or rejects) the CONNECT; the +UUMQTTC
        # URC (28.6) has to be sent from here, not after connect() returns.
        if reason_code == 0:
            print(f"MQTT connected to {self.mqtt_server}:{self.mqtt_port} as '{self.mqtt_client_id}'", flush=True)
            self._set_healthy(True)
            self.send("\r\n+UUMQTTC: 1,0\r\n")
        else:
            print(f"MQTT connect rejected by broker: {reason_code}", flush=True)
            self.mqtt_client = None
            self._set_healthy(False)
            self._abandon(client)
            self.send("\r\n+UUMQTTC: 1,3\r\n")

    def _on_mqtt_disconnect(self, client, userdata, flags, reason_code, properties):
        print(f"MQTT disconnected: {reason_code}", flush=True)
        self._set_healthy(False)

    @staticmethod
    def _abandon(client):
        # loop_stop() joins the network thread; calling it here would
        # deadlock, since this callback runs on that same thread.
        threading.Thread(target=client.loop_stop, daemon=True).start()

    def mqtt_disconnect(self):
        if self.mqtt_client:
            self.mqtt_client.disconnect()
            self.mqtt_client.loop_stop()
            self.mqtt_client = None

    def mqtt_publish(self, topic, message, qos, retain):
        if not self.mqtt_client or not self.mqtt_client.is_connected():
            print(f"Publish to '{topic}' requested but MQTT is not connected, dropping", flush=True)
            return
        self.mqtt_client.publish(topic, message, qos=qos, retain=bool(retain))
        print(f"Published to '{topic}': {message}", flush=True)


def connect():
    while True:
        try:
            sock = socket.create_connection((RENODE_HOST, RENODE_PORT), timeout=10)
            # create_connection()'s timeout lingers on the socket for every
            # future recv() too; clear it so idle gaps aren't mistaken for a dead link.
            sock.settimeout(None)
            print(f"Connected to Renode UART1 relay at {RENODE_HOST}:{RENODE_PORT}", flush=True)
            return sock
        except OSError as exc:
            print(f"Waiting for Renode UART1 relay ({exc}), retrying in 3s...", flush=True)
            time.sleep(3)


def main():
    while True:
        sock = connect()
        modem = Modem(sock)
        try:
            modem.run()
        except (ConnectionError, OSError) as exc:
            print(f"Lost UART1 connection ({exc}), reconnecting...", flush=True)
        finally:
            sock.close()
            modem.mqtt_disconnect()


if __name__ == "__main__":
    main()
