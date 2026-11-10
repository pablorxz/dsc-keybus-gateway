# Development and testing

Ordinary checks do not access an alarm or ESP. For an isolated device, see the
[bench guide](bench.md). [Architecture](architecture.md) describes layer ownership.

## Windows host tests

With Python 3.12, install the pinned Zig compiler for desktop tests:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements/dev.txt
.\.venv\Scripts\python.exe tools/verify_upstream.py
.\.venv\Scripts\python.exe tools/run_tests.py
.\.venv\Scripts\python.exe tools/run_tests.py --own-core
.\.venv\Scripts\python.exe tools/run_tests.py --application
```

The first suite characterizes the original code in `tests/reference-taligentx/`;
the second tests the owned core and its fixes; the third tests the service with
the scripted panel model. Frames and Arduino functions are synthetic. Results are
written to the Git-ignored `build/` directory.

## Linux CMake tests

With Python 3, CMake, and GCC or Clang installed:

```sh
cmake -S . -B build/native -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
```

Suites can also run directly with `python3 tools/run_tests.py --compiler g++
--sanitize`; add `--own-core` or `--application` for the other suites. Sanitizer
validation requires Linux.

## ESP32 baseline compile

This compiles the original reference with Arduino-ESP32 2.x, separately from the
ESPHome firmware. It does not upload anything to a board:

```powershell
.\.venv\Scripts\python.exe -m pip install -r requirements/build.txt
.\.venv\Scripts\python.exe -m platformio run -e esp32-baseline
```

The ESPHome firmware uses a separate environment with `requirements/firmware.txt`.
Configuration and opt-in board checks are described in the [bench guide](bench.md);
see [OTA updates](ota.md) for the upload and recovery model.

## Dashboard and CI

The dashboard wraps ESPHome's Digest verifier with issued, expiring challenges,
request-URI checks and a bounded replay window. Run the offline policy test with
the CMake `digest_guard` test. The guard limits reuse of accepted authorizations;
Digest `qop=auth` does not authenticate the POST body or provide encryption.
It is not a substitute for HTTPS with certificate verification. The ESPHome
firmware compile checks integration; the policy test alone does not validate
browser behavior or provide a full security audit.

The dashboard can optionally keep a disarm code in page memory until reload or
close; it offers an explicit forget control and does not store that code in
browser storage. Anyone with access to that open page can use the remembered
code. Arming includes the remembered code when available; otherwise, if the
panel requests one, the dashboard can supply it to the same pending operation.
Abandoning or losing the prompt leaves the panel state uncertain; inspect the
physical keypad. The Home Assistant entity requests a code for arming as well as
disarming. Standard HTTP examples do not encrypt code submissions.

After editing `web/`, regenerate and verify the embedded assets:

```sh
python tools/build_web_assets.py
python tools/build_web_assets.py --check
```

`tests/integration/dashboard.js` exercises the interface against a synthetic
localhost-only server. It requires Node.js, Playwright, and Chromium;
`.github/workflows/test.yml` specifies the versions and variables used in CI. CI
also checks the reference compile, simulated firmware, and bench tooling without
accessing hardware.

## Upstream reference and provenance

`tools/verify_upstream.py` checks the original files against
`tests/reference-taligentx.lock.json` without network access. Importing with
`tools/vendor_upstream.py --fetch` explicitly replaces the pinned reference; any
revision change requires reviewing its contents, attribution, and tests. Record
porting decisions in [ports.md](ports.md).
