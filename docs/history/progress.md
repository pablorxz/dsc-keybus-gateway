# Implementation progress

> Historical development log. Earlier in-progress statements reflect the time they were written; see the final milestone below and `docs/physical-validation.md` for later results.

## Active milestone

Goal activated on 2026-10-27: implement the first end-to-end prototype described
in AGENTS.md. The user has authorized USB uploads and device/OTA tests on the
explicitly identified isolated ESP32. The production panel remains excluded.

## Starting evidence

- Vendor: 15 original files, pinned master `ce12e1c05c1d`.
- Baseline: 47 host scenarios; independent develop comparison recorded in
  `upstream-review-2026-10-27.md`.
- ESP32 USB bootloader identification succeeded; exact bench identity is in an
  ignored local manifest. No firmware has yet been changed for this goal.
- Docker's Linux engine is unavailable at start. Host Python and compiler are
  available; this does not block core/application work.

## Original planned steps

1. Extract our owned core without modifying vendor and retain shared regression
   tests against actual capture/decode/transmit behavior.
2. Prove a minimal host + pinned ESPHome build of the library layout.
3. Correct write readiness and introduce owned bounded nonblocking TX admission.
4. Implement the shared service and synthetic end-to-end scenarios.

## Evidence policy

Append commands/results and unresolved limitations at each milestone. A compiled
or simulated command is never reported as a successful physical panel operation.

## Milestone 001 — Owned implementation extracted

- Created `lib/dsc_core` with actual lifecycle/capture/transmit/decode code,
  namespace `dsc` and defined instance initialization. No forwarding wrapper.
- Moved static storage into a `.cpp`; excluded Classic and printing from the active
  core. Preserved panel/module capture and the original baseline in vendor.
- Ran `python tools/run_tests.py --own-core --compiler zig`: **46/46 shared
  scenarios pass**. Printing is intentionally excluded. The known original write
  readiness limitation is still characterized before a separate correction.
- Added a distinct owned-core host target and CI checks.
- Bench Wi-Fi credentials have been supplied in the ignored local test file.

## Milestone 002 — Simulated end-to-end prototype (in progress)

- `lib/dsc_core` owns capture, decoding, snapshot validity, a bounded key FIFO,
  disconnect recovery and ESP32-specific hardware support. `lib/dsc_app` owns
  serialized operations and confirmation; HA and HTTP both use that service.
- ESPHome 2026.10.0, Arduino ESP32 3.3.12 / ESP-IDF 5.5.6 and the local library
  resource mechanism compiled and linked successfully. The test build uses only
  replay frames; no physical GPIO transport is started. Flash 917,415 / 1,835,008
  bytes on an earlier build; latest build uses 919,831 / 1,835,008 bytes of flash
  and 52,036 / 180,736 bytes of reported static RAM. Updated UI assets: 6,275 compressed bytes.
- Windows Zig: baseline 47 scenarios, owned core 65; app 15 with GCC ASan/UBSan,
  including an admission test that submits from twelve simultaneous threads. The
  1,000-cycle app
  scenario now starts across the 32-bit clock wrap and interleaves ten rejected
  codes and ten missing-confirmation timeouts with successful cycles. GitHub's GCC
  and Clang sanitizer jobs passed on the earlier cycle scenario; their run for the
  updated scenario remains to be checked.
- On the authorized isolated ESP, authenticated OTA and postboot HTTP/native API
  tests passed, including invalid auth, CSRF checks, shared zone and operation
  states, disconnect/unknown and recovery. A disposable Home Assistant 2026.10.3
  instance verified real native API integration, zone changes, synthetic arm/disarm
  and unknown/recovery. All alarm codes used in those tests are synthetic.
- Native ESPHome lacks a published unknown alarm state. The adapter emits a
  one-byte unrecognized enum, which the tested aioesphomeapi/HA versions map to
  unknown; this compatibility technique is version-dependent and must be retested
  on upgrades. An earlier value >=128 caused malformed protobuf encoding; fixed and
  verified on the device and HA. The initial web startup order also caused a boot
  failure before networking was initialized; fixed and verified after OTA.
- Playwright and manual screenshot review covered 1440px desktop and 390px mobile,
  no horizontal overflow, simulation banner and dialog; no frontend runtime/CDN.
- `tests/integration/dashboard.js` also exercised both sizes against a local-only
  synthetic HTTP server: unknown indicators/zones, disabled disconnected controls,
  command method/header/body, asynchronous result, and no horizontal overflow. It
  uses a local Node.js, Playwright and Chromium installation via `DSC_PLAYWRIGHT`
  and `DSC_CHROME`; this test does not connect to any ESP or Home Assistant.
- A new offline UI regression reproduced stale zone/indicator displays after an
  HTTP failure; the dashboard now clears their validity on that failure and the
  same desktop/mobile test passes. Target compilation and a renewed soak follow.
- An incomplete HTTP request was held open while a second authenticated state
  request ran on the isolated device. The second request succeeded in 4.95 seconds,
  so a stalled client can delay HTTP responses; this is a known prototype limitation
  and is not evidence of real-time responsiveness. UI request timeout was raised
  from four to eight seconds; it cannot guarantee availability under multiple stalls.
- A first 45-minute device soak stopped early on a reset HTTP connection and did
  not pass. The device still answered the encrypted API. A second run uses fresh
  HTTP connections instead of reusing a potentially closed idle connection. It
  collected stable samples but was intentionally stopped before completion when
  GPIO configuration changed. A new run started after rebuilding, OTA and repeating
  the web and HA checks, but was stopped for further UI and HA result changes.
  A fresh run began at approximately 05:14 local on 2026-10-27 against firmware
  from commit `91f6b97`, after successful OTA and web/HA checks. It was stopped
  early when a stale-partition-state bug was reproduced and corrected. OTA and
  integration checks were repeated. A new soak started at approximately 05:31
  local against firmware commit `29798b2` but failed on an HTTP timeout around
  seven minutes. Subsequent serial capture showed repeated HTTP task stack
  overflow and CPU resets. The bounded catalog copy was moved to heap storage and
  a stack watermark diagnostic added. On the revised bench build, 150 authenticated
  state reads and then web/HA tests passed; a later high-water measurement was
  932 bytes free. An authenticated OTA followed by web and HA checks passed. A
  new soak began at approximately 05:45 local on 2026-10-27 against firmware
  commit `1ffdac2`, but was intentionally stopped when the UI stale-state issue
  was found. After OTA and web, HA and visual checks, a fresh run began at about
  05:51 local against firmware commit `52603f8`; it failed on an HTTP timeout.
  A later serial capture showed repeated Wi-Fi association failures followed by
  reconnection, but it began after the timeout and does not prove whether a reset
  also occurred. A subsequent 30-request test passed. Another run started around
  05:55 local on the same firmware with redacted live serial diagnostics enabled;
  it stopped after about 16 minutes when HTTP timed out and the device also stopped
  responding to ping/API. Its last in-run uptime was 1,260,188 ms; a subsequent
  serial observation showed 1,320,203 ms, so the ESP continued running across this
  interval. No reset/Wi-Fi event was recorded in the short timeout capture; the
  cause of the network loss is not established. Diagnostics now observe 30 seconds
  after a timeout, without relaxing pass criteria. A subsequent attempt did not
  start: encrypted identity verification timed out after 60 seconds. The host's
  route to the bench IP goes through a router between subnets; this observation
  does not locate the failure. A bounded identity watch checks the one authorized
  bench address for three successive encrypted connections before another soak.
  Those checks passed and a fresh soak began around 06:20 local with unchanged
  firmware `52603f8`. It stopped after the first minute on an HTTP timeout. No
  reset or Wi-Fi diagnostic was recorded during the subsequent 30-second drain;
  a later three-packet ping to the explicit bench IP returned two replies and one
  timeout. This establishes intermittent reachability, not which network segment
  or component caused it. After another three successful identity checks, a new
  run began around 06:24 local with unchanged `52603f8` firmware. This is preliminary
  evidence, not a completed soak. `tools/analyze_soak.py` independently checks
  completed logs; synthetic tests verify it rejects incomplete runs, excessive
  stack/heap loss, stale stack measurements, and uptime resets (including before
  the first sample) without touching hardware. Uptime is checked against elapsed
  run time with a two-second tolerance for clock drift and rounding.
  Before starting, limits were declared: no
  uptime reset, <=90 seconds between metrics, warmup of five samples, <=12 KiB drop
  from warmup free/largest heap and >=16 KiB largest free block. The next run also
  requires at least 512 bytes of HTTP task stack headroom and 20 stack samples. It is not yet a
  passing result; private logs are in the ignored local bench directory.
- The physical transport now requires explicit clock/read GPIO configuration;
  writing is disabled unless a write GPIO is provided. Configuration tests cover
  shared data GPIO and reject pin conflicts or input-only write pins. These tests
  do not validate actual wiring or electrical compatibility.
- Reconnect now keeps a partition unknown when the first message is only a prompt,
  discards old armed/alarm/delay flags, and recognizes explicit initial alarm or
  delay messages. Trouble and fire have separate validity from partition state;
  until a conclusive message is received, unknown is intentional. After OTA,
  integration tests verified the indicators remain unknown in both web and native
  HA sensors after reconnect, and the UI passed desktop/mobile checks. Earlier
  HTTP tests timed out and reset the API; subsequent serial capture established
  HTTP task stack overflows during the failed soak. The mitigation above still
  needs prolonged validation.
- A native text entity for the latest shared operation outcome compiled and was
  verified in real disposable HA with separate simulated arm/disarm successes.
  It is a snapshot and can skip intermediate results when operations overlap, not
  an event audit trail. Rejection before admission appears in logs, not this sensor.
- Web Digest authentication is not TLS. The alarm code in a command body is
  plaintext in transit, and Digest does not provide complete replay protection.
  Do not use this build as a production security interface. No Keybus electrical,
  timing, physical-keypad coexistence or real panel tests have been performed.
- Next: finish soak and record measured evidence, rerun Linux/Clang sanitizer and
  baseline checks, validate target CI, review remaining command/transport coverage,
  update the port ledger and make reviewable commits.


## Milestone 003 — first prototype verification complete

The isolated bench run begun around 06:24 completed the full 2,700 seconds.
`tools/analyze_soak.py` verified 45 memory samples and HTTP polls, 39 HTTP stack
samples, minimum stack headroom of 884 bytes, warmup/final free heap of
182,736/184,624 bytes and largest block of 110,592/110,592 bytes. The private
diagnostic log was empty during that completed run. The previous interrupted
and failed runs remain documented above; the cause of intermittent reachability
has not been established. The heap-backed HTTP snapshot is validated by this run.

The final evidence report is `docs/history/final-evidence.md`; GitHub Actions run
40495292913 passed all six jobs, and its code and test inputs match the final
reviewed runtime. Vendor integrity was rechecked. Future work requires a separate
physical-panel validation with the user's authorization, and a secure deployment
transport before any production use. No alarm or production ESP was contacted.
