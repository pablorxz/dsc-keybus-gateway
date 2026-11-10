"""Validate transport configuration using synthetic credentials; never flash."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
folder = root / '.local' / 'ci'
source = (folder / 'bench.yaml').read_text(encoding='utf-8')
cases = {
    'simulation': ('  simulation: true', True),
    'physical-needs-pins': ('  physical_bus: true', False),
    'physical-shared': ('  physical_bus: true\n  clock_pin: 22\n  read_pin: 21\n  write_pin: 21', True),
    'clock-collision': ('  physical_bus: true\n  clock_pin: 21\n  read_pin: 21', False),
    'input-only-write': ('  physical_bus: true\n  clock_pin: 22\n  read_pin: 21\n  write_pin: 35', False),
}
temporary = folder / 'config-test.yaml'
try:
    for name, (replacement, expected) in cases.items():
        temporary.write_text(source.replace('  simulation: true', replacement, 1), encoding='utf-8')
        result = subprocess.run([sys.executable, '-m', 'esphome', 'config', str(temporary)],
                                cwd=root, capture_output=True, text=True)
        if (result.returncode == 0) != expected:
            raise SystemExit(f'FAIL {name}: unexpected validation outcome')
        print(f'PASS {name}')
finally:
    temporary.unlink(missing_ok=True)
