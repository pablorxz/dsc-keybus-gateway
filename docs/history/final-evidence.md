# First prototype: final evidence and boundaries

> Historical report of the first milestone. Later physical-panel tests are documented in `docs/physical-validation.md`.

This report covers the first milestone in `AGENTS.md`, not production readiness.
The PC1864 and installed ESP were never used as test targets.

## Automated and simulated evidence

- The original reference is preserved: `python tools/verify_upstream.py` verified
  all 15 files against upstream `ce12e1c05c1d`. Its 47 characterization scenarios
  remain distinct from the 66 scenarios for our core. The 15 application scenarios
  include 1,000 deterministic mixed operation cycles, negative results, clock
  wraparound, bounded queues and concurrent submissions.
- An earlier GitHub Actions run
  passed all six jobs on `b77e532`: Windows, GCC and Clang sanitizers, offline
  dashboard, legacy Arduino baseline and the pinned ESPHome target. Runtime,
  tests, build inputs and workflow match that revision through `e67b5c9`.
- The synthetic panel drives the actual decoder and service. Web and native API
  checks cover authentication, commands, state, unknown and reconnection. A
  disposable Home Assistant 2026.10.3 instance verified entity discovery and
  simulated operations, including the latest asynchronous result sensor. That
  sensor is a snapshot, not an event history; simultaneous outcomes may be missed,
  and rejections before admission are logged instead.
- Dashboard assets are local and total 6,275 compressed bytes. Desktop and
  390-pixel mobile views were checked visually and with browser tests. An
  incomplete HTTP client delayed another request by 4.95 seconds in the slow
  client test; this is a documented responsiveness limitation.

## Isolated ESP32 bench evidence

The isolated board's hardware identity was verified before upload. Runtime sources correspond to `52603f8` and are unchanged
through the final reviewed revision. The local log records an encrypted authenticated OTA handshake and upload,
followed by web, native API and HA checks. Firmware size was 919,831 / 1,835,008 flash bytes;
reported static RAM was 52,036 / 180,736 bytes.

The completed soak met thresholds declared before the run. The independent
`tools/analyze_soak.py` check returned:

| Measurement | Result |
| --- | --- |
| Duration | 2,700 seconds |
| Memory samples / successful HTTP polls | 45 / 45 |
| HTTP stack samples / minimum headroom | 39 / 884 bytes |
| Free heap, warmup / final | 182,736 / 184,624 bytes |
| Largest free block, warmup / final | 110,592 / 110,592 bytes |
| Logged diagnostics during completed run | None |

The analyzer checks sample continuity, uptime, stack and heap limits; local logs
remain ignored and may contain sensitive bench details. Earlier failed and
interrupted attempts, including an HTTP task stack overflow and intermittent
network reachability, remain documented in `docs/history/progress.md` and
`docs/history/acceptance-evidence.md`. The overflow was addressed before this passing
run; the earlier reachability issue was not conclusively diagnosed.

## What remains untested or unsuitable for production

The bench uses synthetic panel data. It cannot establish real Keybus electrical
levels or timing, coexistence with keypads, actual panel decoding or command
acceptance. A later physical validation requires separate authorization. HTTP
Digest authentication does not encrypt traffic or fully prevent replay; command
bodies can expose alarm codes. A secure deployment transport is required before
production. The Home Assistant unknown-state mapping depends on the tested
ESPHome/HA versions and must be revalidated on upgrades. No panic, fire or siren
activation was tested or included in the first milestone.
