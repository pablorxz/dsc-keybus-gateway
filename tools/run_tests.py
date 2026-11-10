"""Build the actual upstream C++ against host GPIO/timer doubles and run scenarios."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from verify_upstream import ROOT, verify


def build(compiler, sanitize, source_dir=None, build_dir=None, without_diagnostics=False, own_core=False, application=False):
    directory = build_dir or ROOT / "build/host"
    source_dir = source_dir or ROOT / ("lib/dsc_core/src" if own_core else "tests/reference-taligentx/src")
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / ("keybus_tests.exe" if os.name == "nt" else "keybus_tests")
    if compiler == "auto":
        compiler = "zig" if os.name == "nt" else "g++"
    if compiler == "zig":
        if importlib.util.find_spec("ziglang") is None:
            raise RuntimeError("Install requirements/dev.txt in a virtual environment first.")
        command = [sys.executable, "-m", "ziglang", "c++"]
        if os.name == "nt": command += ["-target", "x86_64-windows-gnu"]
    else:
        location = shutil.which(compiler)
        if location is None: raise RuntimeError(f"Compiler not found: {compiler}")
        command = [location]
    command += ["-std=c++17", "-O1", "-g", "-DESP32", "-Wall", "-Wextra",
                "-Itests/support", "-Itests/fixtures", "-I" + str(source_dir)]
    if without_diagnostics:
        command += ["-DKEYBUS_NO_DIAGNOSTICS"]
    if own_core:
        command += ["-DDSC_OWN_CORE"]
    if application:
        command += ["-DDSC_ENABLE_REPLAY", "-Ilib/dsc_app/src", "-Ifirmware/simulator/src"]
    if sanitize:
        if os.name == "nt": raise RuntimeError("Use Linux for ASan/UBSan validation.")
        command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    command += ["tests/application/service_tests.cpp" if application else "tests/keybus_tests.cpp", "tests/support/fake_arduino.cpp"]
    if own_core:
        command += [str(file) for file in sorted(source_dir.rglob("*.cpp"))]
        if application:
            command += [str(file) for file in sorted((ROOT / "lib/dsc_app/src").glob("*.cpp"))]
            command += ["firmware/simulator/src/scripted_panel.cpp"]
        else:
            command += ["tests/core/core_regressions.cpp"]
    else:
        command += [str(source_dir / "dscKeybusInterface.cpp"), str(source_dir / "dscKeybusProcessData.cpp")]
    if not without_diagnostics and not own_core:
        command += [str(source_dir / "dscKeybusPrintData.cpp")]
    command += ["-o", str(executable)]
    print(f"Building {'owned core' if own_core else 'real upstream sources'} with {compiler}...", flush=True)
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=300)
    (directory / "compiler.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"Compiler failed; see {directory / 'compiler.log'}\n" + result.stderr[-6000:])
    return executable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", choices=["auto", "zig", "g++", "clang++"], default="auto")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--binary", type=Path, help="Run an already compiled test binary.")
    parser.add_argument("--scenario", help="Run just one named scenario.")
    parser.add_argument("--source-dir", type=Path, help="Evaluate a separate source snapshot without replacing vendor.")
    parser.add_argument("--build-dir", type=Path, help="Separate output directory for a source comparison.")
    parser.add_argument("--without-diagnostics", action="store_true", help="Omit APIs removed from the develop core; report the reduced scenario set.")
    parser.add_argument("--own-core", action="store_true", help="Run shared scenarios against our maintained core, separately from vendor.")
    parser.add_argument("--application", action="store_true", help="Run service scenarios with the shared scripted raw-frame panel model.")
    args = parser.parse_args()
    verify()
    if args.application:
        args.own_core = True
    if args.own_core:
        args.without_diagnostics = True
    directory = args.build_dir.resolve() if args.build_dir else ROOT / ("build/application" if args.application else "build/core" if args.own_core else "build")
    executable = args.binary.resolve() if args.binary else build(
        args.compiler, args.sanitize,
        args.source_dir.resolve() if args.source_dir else None,
        directory if args.build_dir or args.own_core else None,
        args.without_diagnostics, args.own_core, args.application)
    scenarios = subprocess.check_output([str(executable), "--list"], text=True, timeout=10).splitlines()
    if args.scenario:
        if args.scenario not in scenarios: raise RuntimeError("Unknown scenario: " + args.scenario)
        scenarios = [args.scenario]
    outcomes = []
    for scenario in scenarios:
        started = time.monotonic()
        try:
            result = subprocess.run([str(executable), scenario], text=True, capture_output=True, timeout=5)
            passed, output = result.returncode == 0, result.stdout + result.stderr
        except subprocess.TimeoutExpired:
            passed, output = False, "Scenario exceeded 5 seconds (possible blocked primitive)."
        outcomes.append({"scenario": scenario, "passed": passed,
                         "seconds": round(time.monotonic() - started, 4), "output": output.strip()})
        print(("PASS " if passed else "FAIL ") + scenario, flush=True)
        if not passed: print(output.strip(), flush=True)
    report = directory / "test-results.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(outcomes, indent=2) + "\n", encoding="utf-8")
    passed_count = sum(item["passed"] for item in outcomes)
    print(f"{passed_count}/{len(outcomes)} scenarios passed. No hardware was accessed.")
    return 0 if passed_count == len(outcomes) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)

