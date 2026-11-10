"""Render the isolated simulated dashboard using a local Playwright install."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import subprocess
import sys
import yaml
from bench import LOCAL, ROOT, identify_network, manifest

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--expected-mac', required=True)
parser.add_argument('--node', required=True)
parser.add_argument('--playwright', required=True)
parser.add_argument('--chrome', required=True)
args = parser.parse_args()
device = manifest(args.expected_mac)
if 'network_test' not in device.get('authorized_actions', []):
    raise RuntimeError('Network testing not authorized')
asyncio.run(identify_network(device, args.expected_mac))
secrets = yaml.safe_load((LOCAL / 'secrets.yaml').read_text(encoding='utf-8-sig'))
config = {'url': f"http://{device['address']}/", 'chrome': args.chrome,
          'output': str(LOCAL), 'auth': {'username': secrets['web_username'],
                                         'password': secrets['web_password']}}
env = dict(os.environ, DSC_PLAYWRIGHT=str(Path(args.playwright).resolve()))
result = subprocess.run([args.node, str(ROOT / 'tools/bench_visual.js')],
                        input=json.dumps(config), text=True, env=env, cwd=ROOT)
sys.exit(result.returncode)
