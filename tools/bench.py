"""Explicitly targeted, opt-in serial checks/uploads for the isolated test ESP.

This script is never used by CI. The caller supplies the already-authorized MAC;
the local manifest and hardware bootloader must both match it before every upload.
"""
import argparse
import asyncio
import ipaddress
import json
from pathlib import Path
import re
import subprocess
import sys
import time

import serial
import yaml
from aioesphomeapi import APIClient

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / ".local" / "bench"
CONFIG = LOCAL / "bench.yaml"


def manifest(expected):
    data = json.loads((LOCAL / "device.json").read_text(encoding="utf-8-sig"))
    if not data.get("alarm_disconnected") or data.get("mac", "").lower() != expected.lower():
        raise RuntimeError("Bench manifest does not match the authorized disconnected device.")
    if not re.fullmatch(r"COM[1-9][0-9]*", data.get("port", "")):
        raise RuntimeError("Bench manifest must contain one explicit COM port.")
    return data


def identify(data, expected):
    if "identify" not in data.get("authorized_actions", []):
        raise RuntimeError("Identification is not authorized in the bench manifest.")
    command = [sys.executable, "-m", "esptool", "--port", data["port"],
               "--baud", "115200", "--no-stub", "read-mac"]
    run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=45)
    macs = {m.lower() for m in re.findall(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", run.stdout)}
    if run.returncode or macs != {expected.lower()}:
        raise RuntimeError("USB bootloader identity did not match the authorized bench MAC.")
    print(f"Verified isolated bench ESP on {data['port']}: {expected.lower()}", flush=True)


async def identify_network(data, expected):
    address=data.get("address", "")
    try:
        ipaddress.ip_address(address)
    except ValueError as error:
        raise RuntimeError("An explicit bench IP is required for OTA.") from error
    values=yaml.safe_load((LOCAL / "secrets.yaml").read_text(encoding="utf-8-sig")) or {}
    normalized=expected.lower().replace(":", "")
    client=APIClient(address,6053,noise_psk=values["api_encryption_key"],
                     expected_name="dsc-bench",expected_mac=normalized)
    await client.connect(login=True)
    try:
        info=await client.device_info()
        if info.mac_address.lower().replace(":", "")!=normalized:
            raise RuntimeError("The encrypted API reported a different device MAC.")
    finally:
        await client.disconnect()
    print(f"Verified encrypted API identity for {expected.lower()} at {address}",flush=True)


def upload(data, expected, ota=False):
    action="ota" if ota else "upload"
    if action not in data.get("authorized_actions", []):
        raise RuntimeError(f"{action} is not authorized in the bench manifest.")
    binary = ROOT / ("build/esphome-bench/build/firmware.ota.bin" if ota else "build/esphome-bench/build/firmware.factory.bin")
    if not binary.exists() or not CONFIG.exists():
        raise RuntimeError("Compile the bench firmware before uploading.")
    sources = [ROOT / "firmware/bench.yaml", ROOT / "firmware/zones.default.yaml", ROOT / "tools/zones_config.py", CONFIG]
    if (LOCAL / "zones.yaml").exists(): sources.append(LOCAL / "zones.yaml")
    for folder in (ROOT / "lib", ROOT / "components", ROOT / "firmware/simulator"):
        sources.extend(path for path in folder.rglob("*") if path.is_file())
    if any(path.stat().st_mtime > binary.stat().st_mtime for path in sources):
        raise RuntimeError("Firmware is older than source/configuration; rebuild before uploading.")
    if ota:
        asyncio.run(identify_network(data,expected))
        target=data["address"]
    else:
        identify(data, expected)
        target=data["port"]
    command = [sys.executable, "-m", "esphome", "upload", str(CONFIG), "--device", target]
    log_path=LOCAL / ("ota.log" if ota else "upload.log")
    with log_path.open("w", encoding="utf-8") as log:
        run = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=300)
    if run.returncode:
        raise RuntimeError(f"Bench {action} failed; inspect the private {log_path.name}.")
    print(f"Bench {action} completed; private upload log saved.", flush=True)


def observe(data, seconds):
    if "serial" not in data.get("authorized_actions", []):
        raise RuntimeError("Serial observation is not authorized in the bench manifest.")
    port = serial.Serial()
    port.port=data["port"]; port.baudrate=115200; port.timeout=0.25
    port.dtr=False; port.rts=False
    output=bytearray()
    try:
        port.open()
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            output.extend(port.read(min(max(port.in_waiting, 1), 4096)))
    finally:
        if port.is_open:
            port.close()
    text=output.decode("utf-8", errors="replace")
    values=yaml.safe_load((LOCAL / "secrets.yaml").read_text(encoding="utf-8-sig")) or {}
    for value in values.values():
        if isinstance(value,str) and len(value)>=4:
            text=text.replace(value,"[redacted]")
    text=re.sub(r"(?im)^.*(?:password|passwd|secret|token|noise.psk|access.?code).*$", "[redacted sensitive log line]",text)
    (LOCAL / "serial.log").write_text(text,encoding="utf-8")
    match=re.search(r"Device IP:\s*(\d+\.\d+\.\d+\.\d+)",text)
    if match:
        data["address"]=match.group(1)
        (LOCAL / "device.json").write_text(json.dumps(data,indent=2)+"\n",encoding="utf-8")
    print(f"Serial observation: {len(output)} bytes; bench address: {match.group(1) if match else 'not observed'}")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action",choices=("identify","upload","ota","observe"))
    parser.add_argument("--expected-mac",required=True)
    parser.add_argument("--seconds",type=int,default=20)
    args=parser.parse_args()
    data=manifest(args.expected_mac)
    if args.action=="identify": identify(data,args.expected_mac)
    elif args.action in ("upload","ota"): upload(data,args.expected_mac,ota=args.action=="ota")
    else: observe(data,max(1,min(args.seconds,60)))


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError,OSError,subprocess.SubprocessError) as error:
        print(str(error),file=sys.stderr)
        raise SystemExit(1)
