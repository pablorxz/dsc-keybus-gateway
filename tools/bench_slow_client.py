"""Check that a stalled HTTP client does not block authenticated state reads."""
import argparse
import asyncio
import socket
import time
import requests
from requests.auth import HTTPDigestAuth
import yaml
from bench import LOCAL, identify_network, manifest

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--expected-mac', required=True)
args = parser.parse_args()
device = manifest(args.expected_mac)
if 'network_test' not in device.get('authorized_actions', []):
    raise RuntimeError('Network test not authorized')
asyncio.run(identify_network(device, args.expected_mac))
credentials = yaml.safe_load((LOCAL / 'secrets.yaml').read_text(encoding='utf-8-sig'))
with socket.create_connection((device['address'], 80), timeout=5) as slow:
    # Deliberately omit the header terminator and keep the connection open.
    slow.sendall(f"GET /api/state HTTP/1.1\r\nHost: {device['address']}\r\n".encode())
    time.sleep(.25)
    start = time.monotonic()
    response = requests.get(f"http://{device['address']}/api/state", timeout=8,
                            headers={'Connection': 'close'},
                            auth=HTTPDigestAuth(credentials['web_username'], credentials['web_password']))
    if not response.ok or not response.json().get('simulated'):
        raise RuntimeError('Authenticated state unavailable while client stalled')
    print(f'PASS state available with a stalled client ({time.monotonic()-start:.2f}s)')
