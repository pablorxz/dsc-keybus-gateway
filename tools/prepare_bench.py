"""Render an isolated bench config. Never reads household configs or secrets."""
import base64
from pathlib import Path
import secrets
import yaml
from zones_config import render

ROOT = Path(__file__).resolve().parents[1]


def main():
    folder = ROOT / ".local/bench"
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "secrets.yaml"
    settings = yaml.safe_load(path.read_text(encoding="utf-8-sig")) if path.exists() else {}
    settings = settings or {}
    if not all(isinstance(settings.get(key), str) and settings[key].strip()
               for key in ("wifi_ssid", "wifi_password")):
        raise SystemExit("Set wifi_ssid and wifi_password in .local/bench/secrets.yaml first.")
    settings.setdefault("api_encryption_key", base64.b64encode(secrets.token_bytes(32)).decode())
    settings.setdefault("ota_password", secrets.token_urlsafe(24))
    settings.setdefault("web_username", "bench")
    settings.setdefault("web_password", secrets.token_urlsafe(24))
    path.write_text(yaml.safe_dump(settings, allow_unicode=True), encoding="utf-8")
    template = (ROOT / "firmware/bench.yaml").read_text(encoding="utf-8")
    target = folder / "bench.yaml"
    target.write_text(render(template, folder / 'zones.yaml').replace("@@ROOT@@", ROOT.as_posix()), encoding="utf-8")
    print("Prepared ignored bench config and generated test-only API/OTA/web credentials.")


if __name__ == "__main__":
    main()
