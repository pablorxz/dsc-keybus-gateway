"""Import an immutable upstream snapshot; ordinary tests never access the network."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "taligentx/dscKeybusInterface"
COMMIT = "ce12e1c05c1d42a0c58268fb06172391f0283a65"
FILES = [
    "LICENSE", "README.md", "library.properties", "library.json",
    "src/dscClassic.cpp", "src/dscClassic.h",
    "src/dscClassicKeypad.cpp", "src/dscClassicKeypad.h",
    "src/dscKeybus.h", "src/dscKeybusInterface.cpp", "src/dscKeybusInterface.h",
    "src/dscKeybusPrintData.cpp", "src/dscKeybusProcessData.cpp",
    "src/dscKeypad.cpp", "src/dscKeypad.h",
]


def fetch(path):
    url = f"https://raw.githubusercontent.com/{REPOSITORY}/{COMMIT}/{path}"
    with urllib.request.urlopen(url, timeout=45) as response:
        content = response.read()
    target = ROOT / "tests/reference-taligentx" / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(content)
    return {"path": path, "sha256": hashlib.sha256(content).hexdigest(), "url": url}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", required=True,
                        help="Explicitly download and replace the pinned vendor snapshot.")
    parser.parse_args()
    with ThreadPoolExecutor(max_workers=5) as executor:
        files = list(executor.map(fetch, FILES))
    manifest = {"repository": f"https://github.com/{REPOSITORY}", "commit": COMMIT,
                "license": "GPL-3.0-or-later", "files": files}
    (ROOT / "tests/reference-taligentx.lock.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (ROOT / "LICENSE").write_bytes((ROOT / "tests/reference-taligentx/LICENSE").read_bytes())
    print(f"Imported {len(files)} original files at {COMMIT}; no source patches applied.")


if __name__ == "__main__":
    main()
