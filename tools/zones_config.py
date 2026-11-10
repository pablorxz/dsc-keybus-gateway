"""Render zone entities from defaults and an optional installation-local YAML."""
from pathlib import Path
import yaml

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'firmware/zones.default.yaml'
CATEGORIES = {'sensor', 'motion', 'door', 'window'}


def load_zones(custom=None):
    defaults = yaml.safe_load(DEFAULT.read_text(encoding='utf-8'))
    settings = yaml.safe_load(Path(custom).read_text(encoding='utf-8-sig')) if custom and Path(custom).exists() else {}
    if settings is None:
        settings = {}
    if not isinstance(settings, dict) or set(settings) - {'visible_zones', 'zones'}:
        raise ValueError('Zone configuration must contain only visible_zones and zones.')
    visible = settings.get('visible_zones', defaults['visible_zones'])
    default_overrides = defaults.get('zones', {})
    local_overrides = settings.get('zones', {})
    if not isinstance(default_overrides, dict) or not isinstance(local_overrides, dict):
        raise ValueError('zones must be a mapping.')
    overrides = dict(default_overrides)
    for number, metadata in local_overrides.items():
        if not isinstance(metadata, dict):
            raise ValueError(f'Invalid metadata for zone {number}.')
        base = overrides.get(number, {})
        if not isinstance(base, dict):
            raise ValueError(f'Invalid default metadata for zone {number}.')
        overrides[number] = {**base, **metadata}
    if not isinstance(visible, list) or any(type(n) is not int or n < 1 or n > 64 for n in visible) or len(set(visible)) != len(visible):
        raise ValueError('visible_zones must be a list of unique zone numbers from 1 to 64.')
    if any(type(n) is not int or n < 1 or n > 64 for n in overrides) or any(n not in visible for n in local_overrides):
        raise ValueError('Zone overrides must use visible numeric zone identifiers.')
    entries = []
    for number in visible:
        metadata = overrides.get(number, {})
        if not isinstance(metadata, dict) or set(metadata) - {'name', 'group', 'type', 'entity_name'}:
            raise ValueError(f'Invalid metadata for zone {number}.')
        name = metadata.get('name', f'Zona {number}')
        entity_name = metadata.get('entity_name', f'Zona {number}')
        group = metadata.get('group', 'Sin asignar')
        category = metadata.get('type', 'sensor')
        if not all(isinstance(x, str) and x.strip() and len(x) <= 80 for x in (name, entity_name, group)) or not isinstance(category, str) or category not in CATEGORIES:
            raise ValueError(f'Invalid name, group or type for zone {number}.')
        entry = {'platform': 'dsc_alarm_panel', 'panel_id': 'dsc_panel', 'kind': 'zone',
                 'zone': number, 'group': group, 'category': category, 'name': entity_name, 'display_name': name}
        if category != 'sensor':
            entry['device_class'] = category
        entries.append(entry)
    return entries


def render(template, custom=None):
    marker = '@@ZONE_ENTITIES@@'
    if template.count(marker) != 1:
        raise ValueError('Firmware template must have exactly one zone marker.')
    entries = load_zones(custom)
    rendered = yaml.safe_dump(entries, allow_unicode=True, sort_keys=False).rstrip() if entries else ''
    rendered = '\n'.join('  ' + line for line in rendered.splitlines())
    return template.replace(marker, rendered)
