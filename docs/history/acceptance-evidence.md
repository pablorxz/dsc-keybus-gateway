# Acceptance evidence

> Historical snapshot of the first milestone, before physical-panel tests. See `docs/physical-validation.md` for later results.

This is the evidence map for the first milestone in `AGENTS.md`. See
`docs/history/final-evidence.md` for the results and their limits. This is not a claim
of production readiness or physical-alarm compatibility.

| Requirement | Evidence and current state |
| --- | --- |
| Original source and attribution | `tools/verify_upstream.py` verifies all 15 pinned files; owned files retain notices. `docs/ports.md` records source and decisions. |
| Baseline and owned decoder | `tools/run_tests.py`: 47 upstream and 66 owned scenarios on Windows; covers checksum, 64 zones, duplicate data, reset, invalidation, queue, reconnect and regressions. Linux sanitizer jobs also exercise these scenarios. |
| Service and command serialization | `tests/application/service_tests.cpp`: 1,000 simulated cycles with failures and clock wrap; distinct timeout, transport and observed confirmation; twelve simultaneous submitters and bounded capacity. Windows and GCC ASan/UBSan passed 15 scenarios. |
| ESPHome target | Pinned ESPHome 2026.10.0 and Arduino ESP32 3.3.12 compiled the bench build. The original Arduino 2.x baseline is a separate CI job. Check latest CI for the current commit. |
| Shared HA/HTTP behavior | `tools/bench_web.py` tests authenticated web/native API states, simulated commands, unknown and recovery. `tools/ha_sandbox_test.py` verifies a disposable Home Assistant instance, including asynchronous result sensor. These checks use the isolated synthetic panel. |
| UI | Local assets: 6,275 compressed bytes. `tools/bench_visual.py` checks desktop and 390px mobile, rendering and dialog; screenshots were inspected. `tests/integration/dashboard.js` independently tests disconnected/unknown states, HTTP failure and the command contract using a loopback-only synthetic server. An incomplete HTTP client delayed another request by 4.95 seconds. |
| Isolated hardware | Device identity is checked in each upload helper. The revised HTTP stack mitigation passed 150 authenticated state reads and postboot web/HA tests on the isolated bench; observed HTTP stack high-water headroom was 932 bytes. Latest compiled firmware: 919,831 / 1,835,008 flash bytes and 52,036 / 180,736 reported static RAM bytes. No physical panel was used. |
| 45-minute soak | **Passed:** 2,700 seconds, 45 memory samples and HTTP polls, 39 stack samples; minimum HTTP stack headroom 884 bytes. Warmup-to-final free heap 182,736 to 184,624 bytes; largest block 110,592 to 110,592 bytes. `tools/analyze_soak.py` independently verified the completed private logs against the predeclared thresholds. No diagnostics were recorded in the completed run. Earlier attempts were not passing: the run against `29798b2` failed after a timeout; serial diagnostics showed HTTP task stack overflow and resets. A mitigation passed short stress; a subsequent run was stopped early for a UI correction. The first run on `52603f8` timed out and later showed Wi-Fi association failures. A second run stopped after about 16 minutes with HTTP, ping and API unavailable; serial uptime increased from 1,260,188 to 1,320,203 ms across the failure, showing the ESP continued running. A further run after three successful encrypted identity checks timed out after one minute; the 30-second diagnostic drain recorded no reset or Wi-Fi event, and a subsequent ping returned two of three packets. The cause of intermittent reachability remains unproven. The successful run is separate from those partial attempts; intermittent reachability remains an unresolved observation. |
| Final review and CI | An earlier GitHub Actions run passed all six jobs on `b77e532`: GCC/Clang with sanitizers, Windows, offline dashboard, original ESP32 baseline and ESPHome prototype, including the mocked connectivity-watch tests. Runtime, tests, build inputs and workflow match that revision through `e67b5c9`. Vendor integrity and diff checks passed at final review. |

The bench firmware was built from the runtime sources at commit `52603f8`;
subsequent commits through `2898f56` changed documentation/test tools rather than
those sources. The local OTA log records an encrypted handshake and successful upload; the
web/native/HA checks then passed. The
artifact and its embedded test credentials remain outside Git.

HTTP is authenticated but not encrypted; it is not a production alarm-control
transport. The HA unknown-state encoding depends on tested ESPHome/HA behavior and
must be retested on upgrades. A disconnected ESP does not test electrical levels,
bus timing, interference with real keypads, or physical-panel interoperability.
