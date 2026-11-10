"""Test a local disposable Home Assistant against the identified replay ESP."""
import argparse
import asyncio
import json
from pathlib import Path
import sys
import time

from aioesphomeapi import APIClient
import requests
from requests.auth import HTTPDigestAuth
import yaml

ROOT=Path(__file__).resolve().parents[1]
LOCAL=ROOT / ".local"
BASE="http://127.0.0.1:18123"


def request(method,url,**kwargs):
    result=requests.request(method,url,timeout=30,**kwargs)
    if not result.ok:
        raise RuntimeError(f"HTTP {result.status_code} from isolated HA endpoint {url.split('?')[0]}")
    return result.json() if result.content else {}


async def verify_device(device,secret,expected):
    client=APIClient(device["address"],6053,noise_psk=secret["api_encryption_key"],
                     expected_name="dsc-bench",expected_mac=expected.lower().replace(":",""))
    await client.connect(login=True)
    try:
        info=await client.device_info()
        if info.mac_address.lower().replace(":","")!=expected.lower().replace(":",""):
            raise RuntimeError("Device identity mismatch")
    finally:
        await client.disconnect()


def main(expected):
    device=json.loads((LOCAL/"bench/device.json").read_text(encoding="utf-8-sig"))
    if not device.get("alarm_disconnected") or device.get("mac","").lower()!=expected.lower():
        raise RuntimeError("Manifest does not identify the authorized isolated device")
    secret=yaml.safe_load((LOCAL/"bench/secrets.yaml").read_text(encoding="utf-8-sig"))
    asyncio.run(verify_device(device,secret,expected))
    client_path=LOCAL/"ha/client.json"
    client=json.loads(client_path.read_text(encoding="utf-8"))
    token=request("POST",BASE+"/auth/token",data={"grant_type":"refresh_token",
                  "refresh_token":client["refresh_token"],"client_id":client["client_id"]})
    client.update(token); client_path.write_text(json.dumps(client,indent=2),encoding="utf-8")
    headers={"Authorization":"Bearer "+client["access_token"]}

    entries=request("GET",BASE+"/api/config/config_entries/entry",headers=headers)
    matching=[e for e in entries if e.get("domain")=="esphome" and "DSC Bench" in e.get("title","")]
    if not matching:
        flow_path=LOCAL/"ha/flow.json"
        if flow_path.exists():
            flow=json.loads(flow_path.read_text(encoding="utf-8"))
        else:
            flow=request("POST",BASE+"/api/config/config_entries/flow",headers=headers,
                         json={"handler":"esphome","show_advanced_options":False})
        url=BASE+"/api/config/config_entries/flow/"+flow["flow_id"]
        if flow.get("step_id")=="user":
            flow=request("POST",url,headers=headers,json={"host":device["address"],"port":6053})
        if flow.get("step_id")=="encryption_key":
            flow=request("POST",url,headers=headers,json={"noise_psk":secret["api_encryption_key"]})
        while flow.get("type")=="form" and flow.get("step_id") in ("confirm","discovery_confirm"):
            flow=request("POST",url,headers=headers,json={})
        if flow.get("type")!="create_entry":
            raise RuntimeError(f"ESPHome config flow did not complete: {flow.get('step_id')} {flow.get('errors')}")
        flow_path.unlink(missing_ok=True)

    def find_entity(domain,fragment):
        states=request("GET",BASE+"/api/states",headers=headers)
        matches=[s for s in states if s["entity_id"].startswith(domain+".") and
                 fragment in s.get("attributes",{}).get("friendly_name","")]
        return matches[0] if len(matches)==1 else None
    def wait_entity(domain,fragment,predicate,description,timeout=35):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            entity=find_entity(domain,fragment)
            if entity and predicate(entity["state"]): return entity
            time.sleep(.25)
        raise RuntimeError(f"Home Assistant did not reach {description}")

    alarm=wait_entity("alarm_control_panel","Alarma simulada",lambda s:s=="disarmed","disarmed")
    prior=wait_entity("sensor","Resultado de operación simulada",lambda s: bool(s),"operation result sensor")
    zone=wait_entity("binary_sensor","Zona 9",lambda s:s=="off","zone 9 closed")
    web=requests.Session(); web.auth=HTTPDigestAuth(secret["web_username"],secret["web_password"])
    web_base=f"http://{device['address']}"
    marker={"X-DSC-Request":"1"}
    def simulate(path,data):
        result=web.post(web_base+path,data=data,headers=marker,timeout=8)
        if not result.ok: raise RuntimeError(f"Simulator returned HTTP {result.status_code}")
    try:
        simulate("/api/simulation/zone",{"zone":"9","open":"1"})
        wait_entity("binary_sensor","Zona 9",lambda s:s=="on","zone 9 open")
        simulate("/api/simulation/zone",{"zone":"9","open":"0"})
        wait_entity("binary_sensor","Zona 9",lambda s:s=="off","zone 9 restored")
        request("POST",BASE+"/api/services/alarm_control_panel/alarm_arm_away",headers=headers,
                json={"entity_id":alarm["entity_id"],"code":"2468"})
        wait_entity("alarm_control_panel","Alarma simulada",lambda s:s=="armed_away","armed away")
        arm_result=wait_entity("sensor","Resultado de operación simulada",lambda s:s.endswith(":succeeded:none") and s!=prior["state"],"arm operation confirmation")
        request("POST",BASE+"/api/services/alarm_control_panel/alarm_disarm",headers=headers,
                json={"entity_id":alarm["entity_id"],"code":"2468"})
        wait_entity("alarm_control_panel","Alarma simulada",lambda s:s=="disarmed","disarmed again")
        wait_entity("sensor","Resultado de operación simulada",lambda s:s.endswith(":succeeded:none") and s!=arm_result["state"],"disarm operation confirmation")
        simulate("/api/simulation/online",{"online":"0"})
        wait_entity("alarm_control_panel","Alarma simulada",lambda s:s in ("unknown","unavailable"),"unknown during disconnect")
        simulate("/api/simulation/online",{"online":"1"})
        wait_entity("alarm_control_panel","Alarma simulada",lambda s:s=="disarmed","recovered state")
    finally:
        # Restore only this opt-in simulator even when an assertion fails.
        try: simulate("/api/simulation/online",{"online":"1"})
        except (RuntimeError,requests.RequestException): pass
    print("PASS disposable HA: native discovery, zone updates, arm/disarm results, unknown on disconnect, recovery.")


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-mac",required=True)
    args=parser.parse_args()
    try: main(args.expected_mac)
    except (RuntimeError,OSError,requests.RequestException,asyncio.TimeoutError) as exc:
        print(f"Isolated HA test failed: {exc}",file=sys.stderr)
        raise SystemExit(1)
