"""Generate synthetic credentials for an offline-only compile; never access devices."""
from pathlib import Path
import base64
import yaml
from zones_config import render

root = Path(__file__).resolve().parents[1]
folder = root / '.local' / 'ci'
folder.mkdir(parents=True, exist_ok=True)
settings = {'wifi_ssid': 'compile-only', 'wifi_password': 'synthetic-only',
            'api_encryption_key': base64.b64encode(bytes(range(32))).decode(),
            'web_username': 'compile-only', 'web_password': 'synthetic-only'}
(folder / 'secrets.yaml').write_text(yaml.safe_dump(settings), encoding='utf-8')
template = (root / 'firmware' / 'bench.yaml').read_text(encoding='utf-8').replace(
    'build/esphome-bench', 'build/esphome-ci')
(folder / 'bench.yaml').write_text(render(template).replace('@@ROOT@@', root.as_posix()), encoding='utf-8')
