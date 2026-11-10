"""Exercise authenticated state reads on an identified isolated simulated ESP."""
import argparse
import asyncio
import time
import requests
from requests.auth import HTTPDigestAuth
import yaml
from bench import LOCAL, identify_network, manifest

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--expected-mac', required=True)
parser.add_argument('--count', type=int, default=150)
args = parser.parse_args()
if args.count < 1 or args.count > 1000:
    raise SystemExit('count must be 1..1000')
device = manifest(args.expected_mac)
if 'network_test' not in device.get('authorized_actions', []):
    raise SystemExit('Network testing is not authorized')
asyncio.run(identify_network(device, args.expected_mac))
secrets = yaml.safe_load((LOCAL / 'secrets.yaml').read_text(encoding='utf-8-sig'))
session = requests.Session()
session.auth = HTTPDigestAuth(secrets['web_username'], secrets['web_password'])
session.headers.update({'Connection':'close'})
start = time.monotonic()
try:
    for i in range(args.count):
        result = session.get(f"http://{device['address']}/api/state", timeout=8)
        if not result.ok or not result.json().get('simulated'):
            raise RuntimeError(f'Unexpected state response at request {i+1}')
    print(f'PASS {args.count} authenticated simulated state reads in {time.monotonic()-start:.1f}s')
finally:
    session.close()
