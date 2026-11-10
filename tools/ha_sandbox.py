"""Start a disposable, loopback-only Home Assistant for bench integration tests."""
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
LOCAL=ROOT / ".local/ha"
NAME="codex-dsc-ha-bench"
IMAGE="ghcr.io/home-assistant/home-assistant:2026.10.3"
LABEL="codex.dsc.bench"


def docker(*args,check=True):
    return subprocess.run(["docker",*args],cwd=ROOT,text=True,capture_output=True,check=check)


def main():
    existing=docker("inspect",NAME,check=False)
    if existing.returncode==0:
        data=json.loads(existing.stdout)[0]
        if data["Config"]["Labels"].get(LABEL)!="true":
            raise RuntimeError("An unrelated container uses the requested sandbox name")
        if not data["State"]["Running"]:
            docker("start",NAME)
        print("Reused the identified DSC Home Assistant sandbox on localhost:18123.")
        return
    config=LOCAL / "config"; config.mkdir(parents=True,exist_ok=True)
    path=config / "configuration.yaml"
    if not path.exists():
        # No default_config, zeroconf, integrations, or production configuration.
        path.write_text("homeassistant:\n  name: DSC bench\nhttp:\n  server_port: 8123\nfrontend:\nconfig:\napi:\nlogger:\n  default: warning\n",encoding="utf-8")
    result=docker("run","-d","--name",NAME,"--label",f"{LABEL}=true", "--restart=no",
                  "--memory=1g","--cpus=2","-p","127.0.0.1:18123:8123",
                  "--mount",f"type=bind,source={config},target=/config",IMAGE)
    print(f"Started isolated Home Assistant sandbox {result.stdout.strip()[:12]} on localhost:18123.")


if __name__=="__main__":
    try: main()
    except (RuntimeError,OSError,subprocess.CalledProcessError) as error:
        print(f"Sandbox start failed: {error}",file=sys.stderr)
        raise SystemExit(1)
