"""Verify vendored bytes against the checked-in upstream lock, without network."""

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def verify():
    manifest = json.loads((ROOT / "tests/reference-taligentx.lock.json").read_text(encoding="utf-8"))
    failures = []
    for entry in manifest["files"]:
        path = ROOT / "tests/reference-taligentx" / entry["path"]
        actual = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
        if actual != entry["sha256"]:
            failures.append(entry["path"])
    if failures:
        raise RuntimeError("Upstream source differs: " + ", ".join(failures))
    print(f"Verified {len(manifest['files'])} unchanged upstream files ({manifest['commit'][:12]}).")


if __name__ == "__main__":
    verify()

