# Repository guidance

DSC Keybus Gateway implements a DSC PowerSeries/ESP32 prototype: an owned Keybus core,
a shared application service, native ESPHome entities and a local web dashboard.
The first simulated and bench milestone is complete. A PC1864 installation has
also had limited user-authorized physical tests; see `docs/physical-validation.md`.
Do not present those tests as comprehensive compatibility or production readiness.

## Structure and responsibilities

- `lib/dsc_core/` owns capture, decoding, transmission, bounded transport queues,
  state validity and the ESP32 timer/GPIO implementation. It must not depend on
  ESPHome, network interfaces, credentials or user zone labels.
- `lib/dsc_app/` owns the shared command contract, bounded operation scheduler,
  state interpretation and zone catalog. All adapter commands use this service.
- `components/dsc_alarm_panel/` connects ESPHome and Home Assistant;
  `components/dsc_web/` provides HTTP and embedded assets. Web sources live in
  `web/`; regenerate `components/dsc_web/generated_assets.h` with
  `python tools/build_web_assets.py` when the sources change.
- `firmware/` contains examples and the isolated simulator. `tests/` and `tools/`
  contain the automated tests, fixtures and opt-in bench tooling.

Keep the bus lifecycle under one owner. ISR work must be bounded and must not
allocate, block, or perform networking. The transport key queue and application
operation scheduler serve different purposes; adapters must not create competing
command queues or transmit directly. Preserve unknown and stale states. An
arm/disarm operation succeeds only when the requested panel state is observed;
transmission alone is not confirmation. Physical keypads cannot be locked by software, so
report uncertainty and never automatically retry an uncertain key sequence.
Zone names and groups are configuration, not DSC partitions.

## Sources and attribution

Keep `tests/reference-taligentx/` byte-identical to the hashes in
`tests/reference-taligentx.lock.json`. It is a baseline for tests, not a runtime
implementation. Preserve original GPL notices and authorship; do not remove the
reference, license or lock without updating the associated tests and provenance.
Record reviewed ports and behavioral decisions in `docs/ports.md`. The historical
[upstream review](docs/history/upstream-review-2026-10-27.md) explains why selected
changes, including PRs #339, #358 and #290, should not be merged blindly.

## Verification and hardware boundaries

- Choose checks appropriate to the change; use `docs/development.md` for commands.
  Preserve separate original-characterization and corrected-core scenarios.
- Run `python tools/verify_upstream.py` before pushing and keep the PlatformIO
  original-reference compile fixture buildable. Do not weaken tests to hide bugs.
- Ordinary tests and CI must not open serial ports, scan networks, flash devices,
  operate real Home Assistant entities or send commands to a physical panel.
- Bench tests are opt-in and limited to an explicitly identified device whose
  physical disconnection from the alarm has been confirmed. Verify device
  identity before uploads; do not use the installed alarm as a fallback.
- Access to an installed panel or its ESP requires explicit user authorization
  for the specific action and target. Do not infer authorization from its address
  or from an earlier unrelated bench test. Do not seed test builds with household
  secrets. Keep credentials, alarm codes and sensitive logs out of Git and output.
- Synthetic frames and an isolated ESP do not establish physical electrical or
  timing compatibility. Keep simulated, bench and physical evidence distinct.
- HTTP Digest does not encrypt traffic or fully prevent replay. Alarm codes in web
  command bodies are exposed on the network. Do not describe it as secure TLS.

Favor concrete, reviewable changes with behavior-level tests where appropriate.
Update relevant documentation when behavior or validation changes; keep historical
evidence in `docs/history/` rather than presenting it as current status.
