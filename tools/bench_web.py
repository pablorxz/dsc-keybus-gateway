"""Exercise web and native API together against the isolated replay-only ESP."""
import argparse
import asyncio
import json
from pathlib import Path
import sys
import time

from aioesphomeapi import APIClient, AlarmControlPanelCommand, AlarmControlPanelInfo
from aioesphomeapi.model import AlarmControlPanelEntityState, AlarmControlPanelState, BinarySensorInfo, BinarySensorState
import requests
from requests.auth import HTTPDigestAuth
import yaml

ROOT=Path(__file__).resolve().parents[1]
LOCAL=ROOT / ".local/bench"


async def main(expected):
    device=json.loads((LOCAL/"device.json").read_text(encoding="utf-8-sig"))
    if not device.get("alarm_disconnected") or device.get("mac","").lower()!=expected.lower():
        raise RuntimeError("Manifest does not identify the authorized isolated device")
    if "network_test" not in device.get("authorized_actions",[]):
        raise RuntimeError("Network testing is not authorized")
    secrets=yaml.safe_load((LOCAL/"secrets.yaml").read_text(encoding="utf-8-sig"))
    client=APIClient(device["address"],6053,noise_psk=secrets["api_encryption_key"],
                     expected_name="dsc-bench",expected_mac=expected.lower().replace(":",""))
    await client.connect(login=True)
    restore_online=False
    try:
        info=await client.device_info()
        if info.mac_address.lower().replace(":","")!=expected.lower().replace(":",""):
            raise RuntimeError("Native API device identity mismatch")
        entities,_=await client.list_entities_services()
        alarms=[e for e in entities if isinstance(e,AlarmControlPanelInfo)]
        zones=[e for e in entities if isinstance(e,BinarySensorInfo) and "Zona 9" in e.name]
        indicators=[e for e in entities if isinstance(e,BinarySensorInfo) and
                    ("Avería simulada" in e.name or "Incendio simulado" in e.name)]
        if len(alarms)!=1 or len(zones)!=1 or len(indicators)!=2:
            raise RuntimeError("Expected alarm, zone and indicator entities were not found")
        states={}; client.subscribe_states(lambda state: states.update({state.key:state}))
        base=f"http://{device['address']}"
        session=requests.Session()
        session.auth=HTTPDigestAuth(secrets["web_username"],secrets["web_password"])
        session.headers.update({"Connection":"close"})

        async def get(path, authenticated=True):
            def request():
                return (session if authenticated else requests).get(base+path,headers={"Connection":"close"},timeout=8)
            return await asyncio.to_thread(request)
        async def post(path,data,headers=None):
            def request():
                return session.post(base+path,data=data,headers=headers if headers is not None else {"X-DSC-Request":"1"},timeout=5)
            return await asyncio.to_thread(request)
        async def wait(predicate,label,timeout=15):
            until=time.monotonic()+timeout
            while time.monotonic()<until:
                if predicate(): return
                await asyncio.sleep(.1)
            raise RuntimeError(f"Timed out waiting for {label}")
        async def native(target):
            def matches():
                value=states.get(alarms[0].key)
                return isinstance(value,AlarmControlPanelEntityState) and value.state==target
            await wait(matches,f"native state {target}")
        async def web_state(expected_state):
            until=time.monotonic()+15
            while time.monotonic()<until:
                response=await get("/api/state")
                if response.ok:
                    snapshot=response.json()
                    if snapshot["partitions"][0]["state"]==expected_state: return snapshot
                await asyncio.sleep(.15)
            raise RuntimeError(f"Timed out waiting for web state {expected_state}")

        if (await get("/api/state",False)).status_code!=401:
            raise RuntimeError("Unauthenticated state request was accepted")
        wrong=requests.get(base+"/api/state",auth=HTTPDigestAuth("invalid","invalid"),headers={"Connection":"close"},timeout=8)
        if wrong.status_code!=401:
            raise RuntimeError("Wrong web credentials were accepted")
        if (await get("/")).status_code!=200:
            raise RuntimeError("Authenticated dashboard did not load")
        initial=await web_state("disarmed")
        if not initial["simulated"] or len(initial["zones"])!=10:
            raise RuntimeError("Expected simulated dashboard with ten configured zones")
        if (await post("/api/command",{"kind":"arm_away","partition":"1"},headers={})).status_code==200:
            raise RuntimeError("Write without anti-CSRF header was accepted")
        cross={"X-DSC-Request":"1","Origin":"http://untrusted.example"}
        if (await post("/api/command",{"kind":"arm_away","partition":"1"},headers=cross)).status_code==200:
            raise RuntimeError("Cross-origin write was accepted")
        if (await post("/api/command?code=not-a-real-code",{"kind":"arm_away","partition":"1"})).status_code==200:
            raise RuntimeError("Command with query string was accepted")

        for value in ("1","0"):
            if (await post("/api/simulation/zone",{"zone":"9","open":value})).status_code!=200:
                raise RuntimeError("Simulated zone action rejected")
            await wait(lambda: isinstance(states.get(zones[0].key),BinarySensorState)
                       and states[zones[0].key].state==(value=="1"),f"zone 9={value}")
            snapshot=await web_state("disarmed")
            if next(z for z in snapshot["zones"] if z["number"]==9)["open"]!=(value=="1"):
                raise RuntimeError("Native and web zone states differ")

        response=await post("/api/command",{"kind":"arm_away","partition":"1","code":"2468"})
        if not response.ok or not response.json().get("id"):
            raise RuntimeError("Web arm command was not admitted")
        await native(AlarmControlPanelState.ARMED_AWAY); await web_state("armed_away")
        client.alarm_control_panel_command(alarms[0].key,AlarmControlPanelCommand.DISARM,code="2468")
        await native(AlarmControlPanelState.DISARMED); await web_state("disarmed")

        await post("/api/simulation/online",{"online":"0"})
        restore_online=True
        await native(None)
        offline=await web_state("unavailable")
        if offline["connected"] or any(z["open"] is not None for z in offline["zones"]):
            raise RuntimeError("Disconnected web snapshot retained valid zone state")
        await post("/api/simulation/online",{"online":"1"})
        restore_online=False
        await native(AlarmControlPanelState.DISARMED); restored=await web_state("disarmed")
        if restored["trouble"] is not None or restored["partitions"][0]["fire"] is not None:
            raise RuntimeError("Unobserved trouble/fire should remain unknown after reconnect")
        await wait(lambda: all(isinstance(states.get(e.key),BinarySensorState) and states[e.key].missing_state
                               for e in indicators),"native unknown trouble/fire after reconnect")
        print("PASS digest auth, CSRF/query rejection, ten zones, shared native/web state, command sequencing, disconnect/unknown indicators and recovery.")
    finally:
        if restore_online:
            try:
                await asyncio.to_thread(session.post,base+"/api/simulation/online",data={"online":"1"},
                                        headers={"X-DSC-Request":"1"},timeout=5)
            except requests.RequestException:
                pass
        await client.disconnect()


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-mac",required=True)
    args=parser.parse_args()
    try: asyncio.run(main(args.expected_mac))
    except (RuntimeError,OSError,requests.RequestException,asyncio.TimeoutError) as exc:
        print(f"Bench web check failed: {exc}",file=sys.stderr)
        raise SystemExit(1)
