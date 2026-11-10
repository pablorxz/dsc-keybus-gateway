"""Opt-in 45-minute monitoring of the explicitly identified simulated bench ESP."""
import argparse
import asyncio
import re
import sys
import time

import requests
from requests.auth import HTTPDigestAuth
import serial
import yaml

from bench import LOCAL, identify_network, manifest
from soak_diagnostics import classify_diagnostic


def run(expected):
    device = manifest(expected)
    if not {"serial", "network_test"}.issubset(device.get("authorized_actions", [])):
        raise RuntimeError("Serial and network tests must be authorized")
    asyncio.run(identify_network(device, expected))
    secrets = yaml.safe_load((LOCAL / "secrets.yaml").read_text(encoding="utf-8-sig"))
    session = requests.Session()
    session.auth = HTTPDigestAuth(secrets["web_username"], secrets["web_password"])
    # The embedded server may close idle keepalive connections before the next poll.
    session.headers.update({"Connection": "close"})
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = device["port"], 115200, 1
    port.dtr = port.rts = False
    samples = []
    stack_samples = []
    start = time.monotonic()
    next_check = start
    previous_uptime = None
    # Fixed in advance: 45 minutes, samples at least every 90 s, allow 12 KiB
    # drop from the fifth sample and require largest allocatable block >= 16 KiB.
    # One HTTP request/minute exercises authenticated state without commands.
    try:
        port.open()
        with (LOCAL / "soak.log").open("w", encoding="utf-8", buffering=1) as log, \
             (LOCAL / "soak-diagnostics.log").open("w", encoding="utf-8", buffering=1) as diagnostics:
            def record_diagnostic(line):
                diagnostic=classify_diagnostic(line)
                if diagnostic:
                    diagnostics.write(f"elapsed={int(time.monotonic()-start)} {diagnostic[0]} {diagnostic[1]}\n")
                    if diagnostic[0]=='fatal': raise RuntimeError(f"Device reported {diagnostic[1]}")
            log.write("limits: 2700 s; sample gap <= 90 s; warmup=5 samples; "
                      "free and largest drop <= 12288 bytes; largest >= 16384 bytes; "
                      "HTTP stack minimum >= 512 bytes, >= 20 samples\n")
            while time.monotonic() - start < 2700:
                line = port.readline().decode("utf-8", errors="replace")
                record_diagnostic(line)
                match = re.search(r"bench_metrics uptime_ms=(\d+) free=(\d+) largest=(\d+)", line)
                stack_match = re.search(r"bench_http_stack_min_bytes=(\d+)", line)
                now = time.monotonic()
                if stack_match:
                    remaining = int(stack_match.group(1))
                    stack_samples.append(remaining)
                    log.write(f"http_stack elapsed={int(now-start)} min_free={remaining}\n")
                    if remaining < 512:
                        raise RuntimeError("HTTP task stack headroom below 512 bytes")
                if match:
                    uptime, free, largest = map(int, match.groups())
                    if previous_uptime is not None and uptime <= previous_uptime:
                        raise RuntimeError("Device uptime reset during soak")
                    if samples and now - samples[-1][0] > 90:
                        raise RuntimeError("Metrics sample gap exceeded 90 seconds")
                    previous_uptime = uptime
                    samples.append((now, free, largest))
                    log.write(f"sample {len(samples)} elapsed={int(now-start)} uptime={uptime} free={free} largest={largest}\n")
                    if largest < 16384:
                        raise RuntimeError("Largest allocatable block below threshold")
                    if len(samples) > 5 and (samples[4][1]-free > 12288 or samples[4][2]-largest > 12288):
                        raise RuntimeError("Heap declined beyond declared tolerance")
                if not samples and now - start > 90:
                    raise RuntimeError("No metrics received within 90 seconds")
                if now >= next_check:
                    try:
                        response = session.get(f"http://{device['address']}/api/state", timeout=8)
                    except requests.RequestException:
                        # Drain only redacted diagnostic categories. The UART may
                        # have buffered the reset/disconnect while HTTP blocked.
                        # Radio/link failure can be reported after the HTTP
                        # timeout; keep observing briefly without retrying or
                        # changing the run's pass/fail criteria.
                        until=time.monotonic()+30
                        while time.monotonic()<until:
                            record_diagnostic(port.readline().decode("utf-8",errors="replace"))
                        raise
                    if not response.ok or not response.json().get("simulated"):
                        raise RuntimeError("Simulated dashboard state unavailable")
                    log.write(f"http ok elapsed={int(now-start)}\n")
                    next_check = now + 60
            if len(samples) < 44 or time.monotonic()-samples[-1][0] > 90 or len(stack_samples)<20:
                raise RuntimeError("Insufficient continuous metrics")
            log.write(f"PASS samples={len(samples)} stack_samples={len(stack_samples)} "
                      f"min_http_stack={min(stack_samples)} elapsed={int(time.monotonic()-start)}\n")
        print(f"PASS 45-minute bench soak: {len(samples)} memory samples", flush=True)
    finally:
        port.close()
        session.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-mac", required=True)
    try:
        run(parser.parse_args().expected_mac)
    except (RuntimeError, OSError, requests.RequestException, serial.SerialException) as error:
        print(f"Bench soak failed: {error}", file=sys.stderr, flush=True)
        sys.exit(1)
