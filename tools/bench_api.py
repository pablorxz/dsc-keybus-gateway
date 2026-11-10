"""Smoke-test the encrypted native API of the explicitly identified bench ESP."""
import argparse
import asyncio
import json
from pathlib import Path
import sys

from aioesphomeapi import APIClient, AlarmControlPanelCommand, AlarmControlPanelInfo
from aioesphomeapi.model import AlarmControlPanelEntityState, AlarmControlPanelState
import yaml

ROOT=Path(__file__).resolve().parents[1]
LOCAL=ROOT / ".local" / "bench"


async def run(expected_mac, exercise):
    device=json.loads((LOCAL / "device.json").read_text(encoding="utf-8-sig"))
    if not device.get("alarm_disconnected") or device.get("mac", "").lower()!=expected_mac.lower():
        raise RuntimeError("Manifest does not identify the authorized isolated device")
    address=device.get("address")
    if not address:
        raise RuntimeError("Bench address is missing; use the serial observe command first")
    secret=yaml.safe_load((LOCAL / "secrets.yaml").read_text(encoding="utf-8-sig"))
    normalized_mac=expected_mac.lower().replace(":", "")
    client=APIClient(address,6053,noise_psk=secret["api_encryption_key"],
                     expected_name="dsc-bench",expected_mac=normalized_mac)
    await client.connect(login=True)
    try:
        info=await client.device_info()
        if info.mac_address.lower().replace(":", "")!=normalized_mac:
            raise RuntimeError("Native API device MAC did not match")
        entities,_=await client.list_entities_services()
        alarms=[entity for entity in entities if isinstance(entity,AlarmControlPanelInfo)]
        if len(alarms)!=1:
            raise RuntimeError(f"Expected one alarm entity; received {len(alarms)}")
        states={}
        client.subscribe_states(lambda state: states.update({state.key:state}))

        async def wait_for(target,timeout=20):
            end=asyncio.get_running_loop().time()+timeout
            while asyncio.get_running_loop().time()<end:
                state=states.get(alarms[0].key)
                if isinstance(state,AlarmControlPanelEntityState) and state.state==target:
                    return
                await asyncio.sleep(.1)
            observed=states.get(alarms[0].key)
            raise RuntimeError(f"Expected {target.name}; observed {observed}")

        await wait_for(AlarmControlPanelState.DISARMED)
        print(f"Encrypted API authenticated {expected_mac.lower()} at {address}; {len(entities)} entities; initial state disarmed.",flush=True)
        if exercise:
            if "network_test" not in device.get("authorized_actions",[]):
                raise RuntimeError("Network tests are not authorized in bench manifest")
            # This synthetic code belongs exclusively to ScriptedPanel. It is not
            # a user/panel credential and is never printed or stored in results.
            client.alarm_control_panel_command(alarms[0].key,AlarmControlPanelCommand.ARM_AWAY,code="2468")
            await wait_for(AlarmControlPanelState.ARMED_AWAY)
            client.alarm_control_panel_command(alarms[0].key,AlarmControlPanelCommand.DISARM,code="2468")
            await wait_for(AlarmControlPanelState.DISARMED)
            print("Native API simulated arm-away and disarm reached observed states.",flush=True)
    finally:
        await client.disconnect()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-mac",required=True)
    parser.add_argument("--exercise",action="store_true",help="Send commands to the isolated simulator")
    args=parser.parse_args()
    asyncio.run(run(args.expected_mac,args.exercise))


if __name__=="__main__":
    try: main()
    except (RuntimeError,OSError,asyncio.TimeoutError) as exc:
        print(f"Bench API check failed: {exc}",file=sys.stderr)
        raise SystemExit(1)
