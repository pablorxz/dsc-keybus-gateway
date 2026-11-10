"""Render a local physical build. Does not contact or update any device."""
from pathlib import Path
from zones_config import render

root = Path(__file__).resolve().parents[1]
folder = root / '.local/physical-test'
folder.mkdir(parents=True, exist_ok=True)
config = render((root / 'firmware/physical-example.yaml').read_text(encoding='utf-8'), folder / 'zones.yaml')
(folder / 'physical-full.yaml').write_text(config.replace('@@ROOT@@', root.as_posix()), encoding='utf-8')
print('Rendered local physical configuration. No device was contacted.')
