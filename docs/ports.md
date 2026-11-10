# Source and behavior ledger

The owned core derives from taligentx/dscKeybusInterface, revision
`ce12e1c05c1d42a0c58268fb06172391f0283a65`, GPL-3.0-or-later. Original notices are
retained. `tests/reference-taligentx/` remains unchanged and independently executable.

## 001 — PowerSeries extraction

- `dscKeybus.h` becomes `lib/dsc_core/src/keybus.h`, class `dsc::Keybus`.
- The PowerSeries static definitions move out of the public upstream wrapper into
  `keybus.cpp`, eliminating its one-translation-unit include requirement.
- Constructor/lifecycle/loop live in `keybus.cpp`; capture/ISR in `capture.cpp`;
  key encoding/transmission in `transmit.cpp`; state decoding in `decode.cpp`.
- Instance fields receive defined initialization rather than relying on static
  sketch allocation. Only PowerSeries is active; Classic sources remain in vendor.
- Printing methods are not part of the owned primitive API. Optional diagnostics
  will consume frames/state rather than owning a second capture implementation.
- The blocking `setTime` menu operation is excluded; clock setting belongs in the
  application coordinator. Panel time/event decoding remains in the core.
- Shared baseline scenarios compile against the actual owned class using a test
  type alias. There is no runtime adapter or linked original implementation.
- This first extraction intentionally retains the characterized write readiness
  behavior; corrections are recorded as separate changes with new regressions.

## 002 — Refresh write readiness before duplicate-frame early returns

- Problem: the ISR completes a key transmission but repeated 0x05 frames are
  discarded, so loop() can return without setting `writeReady` back to true.
- Correction: refresh readiness at loop entry, independently of decoding a frame.
  The original test still characterizes vendor; the owned-core scenario asserts
  the corrected behavior against the same simulated wire transaction.
- This is our focused correction, informed by the historical Dilbert buffering
  discussion, not a wholesale import of the obsolete upstream PR #290.

## 003 — Bounded transmission and observed state

- Upstream master `ce12e1c05c1d` remains the source of the wire encoding and
  decoding. The Dilbert revisions pinned in AGENTS.md informed investigation of
  write readiness and buffered command sequencing; the queue is our implementation,
  not a claim of a verbatim port. It owns copied payloads, bounds admission, uses
  nonblocking progress and exposes timeout/disconnection independently of delivery.
- Regression scenarios cover ownership, FIFO ordering, full queue, handshake timeout,
  disconnected bus, repeated emergency bytes and the original writeReady issue.
  Application operations separately wait for an observed panel state.
- Reconnection invalidates old zone snapshots; replay and hardware use the same
  decoder. Separate panel-clock and event-clock values avoid treating an event's
  historical timestamp as the current clock. Frame lengths and checksums are checked
  before decoding, including a deterministic malformed-frame stress case.
- After disconnection, a prompt alone cannot revalidate the last partition state:
  old arm/alarm/delay fields are cleared and validity returns only on an explicit
  supported state message. First-observed alarm and entry/exit delays remain visible.
  A partition may remain unknown while only less-specific messages are received.
  Regressions cover prompt-first reconnect and stale alarm followed by fresh armed.
- Trouble and per-partition fire indicators have independent validity. A status
  that identifies a partition but does not establish these lights must not imply
  that either indicator is off. The native sensors and HTTP snapshot publish
  unknown until the corresponding decodable status is observed.
- The original `resetStatus()` assigned `disabled[]` while intending to flag a
  change. Our implementation sets `disabledChanged[]`; the public regression
  `owned-reset-preserves-observation` reproduced the original defect before the fix.
- Shared GPIO data uses open-drain release rather than the inverted separate
  transistor output. This is tested with GPIO doubles only, not a physical Keybus.

## 004 — Comparison with the pinned Dilbert component

Reviewed `components/dsc_alarm_panel/dscKeybusInterface.cpp` and
`dscKeybusProcessData.cpp` at Dilbert66/esphome-components revision
`33911affa459361900176d8688426111cba453e2`. This is a snapshot comparison, not a
claim that all its lines originated in that fork. The relevant upstream code is
available in the [interface at that revision](https://github.com/Dilbert66/esphome-components/blob/33911affa459361900176d8688426111cba453e2/components/dsc_alarm_panel/dscKeybusInterface.cpp)
and [decoder at that revision](https://github.com/Dilbert66/esphome-components/blob/33911affa459361900176d8688426111cba453e2/components/dsc_alarm_panel/dscKeybusProcessData.cpp).

| Candidate | Decision and evidence |
| --- | --- |
| Shared read/write GPIO and separate-output polarity | Retain the capability inside our core. The fork switches GPIO behavior and has `invertWrite`; our implementation uses open drain for a shared pin and the existing separate-transistor polarity. Host GPIO tests prove our logic only; electrical/timing behavior is untested. |
| Queued, copied writes | The fork copies entries to a circular queue. Our different bounded queue reports full/invalid input and owns its payload. Tests cover saturation, ordering, timeout, disconnect and emergency-byte ordering. We did not copy that queue verbatim. |
| Modern timer support | Both implementations adapt ESP32 runtime details. Our conditional timer code is in `platform/esp32` and compiles against the pinned modern target; this is not evidence of real interrupt timing. |
| Binary `setDateTime` / 0xD0 | The fork implements a binary request/response. The first milestone uses the original keypad clock operation through the application scheduler instead; no claim is made that the binary mechanism was ported. A port needs dedicated protocol fixtures and physical timing validation. |
| LCD/programming and expander exchanges | Excluded from the initial milestone; they should not be imported merely to support the lightweight dashboard. |
| Alarm-zone change flags | The compared fork writes the current bit before comparing it in `processAlarmZonesStatus`; our inherited decoder compares with `previousAlarmZones`. `owned-alarm-zone-change-flags` exercises alarm, duplicate and restoration events for zones 1 and 33. This is synthetic evidence, not an assertion that a real installation failed. |

The fork's queue path shown in this snapshot advances the insertion index without
an explicit full result. This observation motivated saturation testing rather than
copying its implementation. The port tests listed here use synthetic frames or GPIO doubles. See
`docs/physical-validation.md` for the later limited physical observations.
