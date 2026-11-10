"""Wait for consecutive authenticated checks of one authorized bench ESP."""
import argparse
import asyncio
import sys
import time
from aioesphomeapi.core import APIConnectionError
from bench import LOCAL, identify_network, manifest


async def watch(expected, timeout_s):
    device=manifest(expected)
    if 'network_test' not in device.get('authorized_actions', []):
        raise RuntimeError('Network testing is not authorized')
    start=time.monotonic()
    successes=0
    with (LOCAL/'connectivity.log').open('w',encoding='utf-8',buffering=1) as log:
        while time.monotonic()-start < timeout_s:
            try:
                await asyncio.wait_for(identify_network(device,expected), timeout=15)
                successes += 1
                log.write(f'elapsed={int(time.monotonic()-start)} authenticated={successes}/3\n')
                if successes==3:
                    print('PASS three consecutive authenticated bench checks', flush=True)
                    return
            except (APIConnectionError, OSError, asyncio.TimeoutError) as error:
                successes=0
                log.write(f'elapsed={int(time.monotonic()-start)} unavailable={type(error).__name__}\n')
            await asyncio.sleep(20)
    raise RuntimeError('Bench identity did not remain reachable during the watch interval')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected-mac',required=True)
    parser.add_argument('--minutes',type=int,default=20,choices=range(1,31))
    args=parser.parse_args()
    try: asyncio.run(watch(args.expected_mac,args.minutes*60))
    except (RuntimeError,OSError) as error:
        print(f'Connectivity watch: {error}',file=sys.stderr,flush=True)
        sys.exit(1)
