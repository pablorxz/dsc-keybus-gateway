# Taligentx review: branches, issues, and pull requests

Review date: October 27, 2026. The objective was to select a baseline and changes
for our PowerSeries/ESP32 core while preserving traceability and hardware-free tests.
This is a historical snapshot; it predates the later physical tests documented in
[physical validation](../physical-validation.md). At review time, the unmodified
reference had not been replaced and no patch had been deployed to production.

## Sources and scope

- [Original repository](https://github.com/taligentx/dscKeybusInterface).
- `master`: `ce12e1c05c1d42a0c58268fb06172391f0283a65`, March 18, 2022.
- `develop`: `ca09256f67462fde0c10073d8c02badfa29ed471`, March 29, 2024.
- [Comparison](https://github.com/taligentx/dscKeybusInterface/compare/ce12e1c05c1d42a0c58268fb06172391f0283a65...ca09256f67462fde0c10073d8c02badfa29ed471): develop was 25 commits ahead and zero behind.
- API inventory: 49 open and 90 closed issues; 6 open, 37 merged, and 18 closed
  unmerged PRs. REST's `open_issues_count=55` includes the six open PRs.
- All issues and PRs were inventoried; issue comments were consulted, and all six
  open PRs plus historical PRs concerning transport, modules, initialization,
  and protocol were examined in detail. Not every historical PR was compiled or
  every reported problem reproduced.
- Raw GitHub data was saved under the Git-ignored `build/upstream-review/`. The
  [inventory without bodies or logs](upstream-inventory-2026-10-27.json) is retained.

## Execution results

| Variant | Check | Result |
| --- | --- | --- |
| Unmodified master | Full suite at review time | 47/47 scenarios |
| Unmodified develop | Shared scenarios without the removed diagnostic API | 46/46 scenarios |
| Develop Reader | Capture and diagnostics in a separate process | Passed |
| Develop | ESP32 compilation with pinned Arduino 2.x | Passed; no firmware uploaded |
| Master plus PR #339 file | 0xEB disarm event at minute 00 | Failed: disarm was lost |
| PR #358 | Preprocess the version conditional with IDF 4 | Failed: `#elif` without an expression |
| PR #358 | Preprocess the same conditional with IDF 5 | Passed; does not demonstrate hardware timing |

`event-at-zero-minute` passes on master and develop. It uses the documented 0xEB
format with a disarm event by user 40 at 10:00, without an access code. Bytes 6
and 7 may be zero for a real event. PR #339 returns before processing this event.
`known-write-ready-staleness` reproduces the same limitation on both branches;
this characterizes the behavior rather than endorsing it. No test in this review
accessed a physical alarm or sent a physical command.

## Changes in develop

1. **Separate Reader and normal usage.** Adds `dscKeybusReader.h/.cpp` and
   `dscKeybusReaderInterface`; detailed diagnostics move there. It is not a
   subclass of the normal controller: capture and write implementations are
   separate. Do not attach two independent capture owners to the same bus.
2. **Smaller normal API.** Removes `print...` methods, module capture, and
   diagnostic options from `dscKeybusInterface`; renames `dscReadSize` to
   `dscDataSize` and `validCRC` to `validChecksum`; removes redundant `yield()`.
3. **Expanded protocol diagnostics and documentation.** PR #283 adds RF5132
   programming (0x8D/0x94), PC5200, auto-disarm, and fault/bypass for zones 33–64.
   Much of this is in `dscKeybusPrintData.cpp` and does not itself expose
   structured state through the normal controller.
4. **Updated examples.** MQTT/Home Assistant, LittleFS, HTTP authentication,
   ArduinoJson 7, certificates, and Blynk are not core-protocol changes.
5. **Expanded Classic/PC3000.** Outside the PC1864 target and not a reason to add
   all variants to the first owned implementation.

### Cost of the Reader split

In [issue #289](https://github.com/taligentx/dscKeybusInterface/issues/289),
Dilbert reports that moving module capture exclusively to Reader breaks consumers
such as wireless sensor battery reporting. The declarations and implementation
confirm the difference independently of the comment. Our core should keep one
capture owner, optional module processing, and diagnostics as a consumer, rather
than copy two separate capture/timer implementations.

## Six open pull requests at review time

| PR | Proposal | Decision for this project |
| --- | --- | --- |
| [#374](https://github.com/taligentx/dscKeybusInterface/pull/374) | `/status` and Telegram chat authorization | Valid example security improvement; keep Telegram outside the core and authenticate external adapters. |
| [#368](https://github.com/taligentx/dscKeybusInterface/pull/368) | Direct switch to Arduino 3.x timers | Migration reference, not a direct port: replaces the old API without conditional compatibility; reported tests used PC1550/Classic. |
| [#363](https://github.com/taligentx/dscKeybusInterface/pull/363) | Fix a space in an OpenHAB topic | Example fix linked to #320; outside the core and not merged upstream. |
| [#362](https://github.com/taligentx/dscKeybusInterface/pull/362) | Add night mode to the OpenHAB example | Outside the core; the `n` key already exists. Related to #361. |
| [#358](https://github.com/taligentx/dscKeybusInterface/pull/358) | Arduino 3.x / IDF 5 timers | Adapt rather than copy: an `#elif` lacks an expression and a user reports ESP32-S2 synchronization problems. Our test demonstrates failure on the IDF 4 path. |
| [#339](https://github.com/taligentx/dscKeybusInterface/pull/339) | Restrict clock updates to timestamp messages | Do not import wholesale: a test shows its 0xEB early return drops a valid disarm at minute 00. Separate panel clock and event date with correct criteria. |

The timer PRs were not validated here on real Arduino 3.x hardware or a physical
bus. Compilation alone cannot prove correct latency or sampling.

## Relevant closed pull requests

| PR | Actual status | What to retain |
| --- | --- | --- |
| [#290](https://github.com/taligentx/dscKeybusInterface/pull/290) | Closed unmerged; Dilbert declared it obsolete | Historical circular-queue and nonblocking-write reference, not a final patch. Its author said it replaced the old expander branch. |
| [#150](https://github.com/taligentx/dscKeybusInterface/pull/150) | Closed unmerged; moved to expander | Timeouts and transient-message filtering; design regressions before adapting. |
| [#147](https://github.com/taligentx/dscKeybusInterface/pull/147) | Closed unmerged | Prior work on 0x05 filtering and waiting after an asterisk; not evidence it landed in master/develop. |
| [#329](https://github.com/taligentx/dscKeybusInterface/pull/329) | Closed unmerged; withdrawn by author | Do not copy. It changes `resetStatus`, including observed state; the author warned something was wrong. Treat correct initialization separately. |
| [#283](https://github.com/taligentx/dscKeybusInterface/pull/283) | Merged into develop | Retain protocol knowledge and fixtures for relevant messages; decide which states to expose. |
| [#299](https://github.com/taligentx/dscKeybusInterface/pull/299) | Merged into develop | LittleFS and authentication belong in the web example/adapter. |
| [#297](https://github.com/taligentx/dscKeybusInterface/pull/297) | Merged into develop | MQTT example update, not a primitive. |
| [#296](https://github.com/taligentx/dscKeybusInterface/pull/296) | Merged into dscClassic | PC3000 support is historical context, outside the PC1864 scope. |
| [#173](https://github.com/taligentx/dscKeybusInterface/pull/173), [#165](https://github.com/taligentx/dscKeybusInterface/pull/165), [#161](https://github.com/taligentx/dscKeybusInterface/pull/161) | Merged into expander, not master | IRAM, supervision, and expander compatibility references; Dilbert said the branch was superseded. |

## Inventory of 49 open issues at review time

Open does not mean a confirmed bug; some entries contain contributions, questions,
or fixes already present in develop. This classification targets PC1864/ESP32.

| Issue | Classification and treatment |
| --- | --- |
| [#373](https://github.com/taligentx/dscKeybusInterface/issues/373) | Gerber files; hardware/documentation, outside the core. |
| [#372](https://github.com/taligentx/dscKeybusInterface/issues/372) | MQTT with a keypad without a panel; integration and a different device mode. |
| [#371](https://github.com/taligentx/dscKeybusInterface/issues/371) | Recent SDK incompatibility; the baseline compiled with pinned SDK 2.x; separate modern migration remained. |
| [#370](https://github.com/taligentx/dscKeybusInterface/issues/370) | Does not rearm after an alarm; no confirmed fix. Add alarm/disarm/rearm scenarios and review example policy. |
| [#369](https://github.com/taligentx/dscKeybusInterface/issues/369) | `*2` writes and keypad coexistence; simulation covers the normal handshake, not physical coexistence. |
| [#367](https://github.com/taligentx/dscKeybusInterface/issues/367) | DSC2500 Classic; outside target. |
| [#366](https://github.com/taligentx/dscKeybusInterface/issues/366) | External ESPHome configuration and timer errors in comments; separate integration from SDK compatibility. |
| [#364](https://github.com/taligentx/dscKeybusInterface/issues/364) | Web keypad presentation; outside the first stage. |
| [#361](https://github.com/taligentx/dscKeybusInterface/issues/361) | Night mode in the OpenHAB example; PR #362 proposed, not integrated. |
| [#360](https://github.com/taligentx/dscKeybusInterface/issues/360) | The complaint about uninitialized `static` locals does not establish that bug: they are zero-initialized. Review automatic objects and `resetStatus` separately. |
| [#357](https://github.com/taligentx/dscKeybusInterface/issues/357) | Concurrency/blocking loop; define one processing owner and avoid heavy ISR work. |
| [#355](https://github.com/taligentx/dscKeybusInterface/issues/355) | Unlocker/PC1500; outside scope. Comments describe replacing the full library when switching to develop. |
| [#354](https://github.com/taligentx/dscKeybusInterface/issues/354) | ESPHome integration for Classic; outside the PowerSeries core. |
| [#353](https://github.com/taligentx/dscKeybusInterface/issues/353) | Lovelace dashboard; presentation. |
| [#349](https://github.com/taligentx/dscKeybusInterface/issues/349) | PC1500 states; Classic, outside scope. |
| [#346](https://github.com/taligentx/dscKeybusInterface/issues/346) | Wiring question; hardware/documentation. |
| [#344](https://github.com/taligentx/dscKeybusInterface/issues/344) | Arduino 3.x timer API; same compatibility area as #358/#368. |
| [#343](https://github.com/taligentx/dscKeybusInterface/issues/343) | Keypad-without-panel/Ethernet example; another mode. |
| [#338](https://github.com/taligentx/dscKeybusInterface/issues/338) | PC3000 zones; author notes experimental support in develop, but the issue remains open. Not applicable to PC1864. |
| [#331](https://github.com/taligentx/dscKeybusInterface/issues/331) | KeybusReader size on Uno; maintainer confirms a develop fix despite the open issue. |
| [#328](https://github.com/taligentx/dscKeybusInterface/issues/328) | Disarming after hours/MQTT reconnect; one user avoids it by switching examples. Not a demonstrated protocol fix. |
| [#326](https://github.com/taligentx/dscKeybusInterface/issues/326) | Virtual expander; optional future extension. |
| [#324](https://github.com/taligentx/dscKeybusInterface/issues/324) | PC1550 Classic/six-digit codes; outside scope, but do not hardcode credential length in transport. |
| [#320](https://github.com/taligentx/dscKeybusInterface/issues/320) | OpenHAB topic with a space; PR #363 proposed, not merged. |
| [#318](https://github.com/taligentx/dscKeybusInterface/issues/318) | PC1864 keypad blocks after ESP connection; comments point to wiring. No demonstrated software fix. |
| [#317](https://github.com/taligentx/dscKeybusInterface/issues/317) | Serial diagnostics alongside web keypad; an observability requirement, not another capture owner. |
| [#315](https://github.com/taligentx/dscKeybusInterface/issues/315) | ArduinoJson compilation in the web example; develop migrates the example, not necessarily every reported case. |
| [#314](https://github.com/taligentx/dscKeybusInterface/issues/314) | Blynk QR; legacy integration outside the core. |
| [#312](https://github.com/taligentx/dscKeybusInterface/issues/312) | Escort temperature emulation; additional feature outside scope. |
| [#304](https://github.com/taligentx/dscKeybusInterface/issues/304) | Passing user codes from HA/MQTT; adapter policy, not implicit transport authentication. |
| [#300](https://github.com/taligentx/dscKeybusInterface/issues/300) | Interpreting stay without entry delay; specific reproduction pending. Do not assume night and away are equivalent. |
| [#295](https://github.com/taligentx/dscKeybusInterface/issues/295) | PC5200/auto-disarm/high-zone contribution included in #283/develop; open does not mean absent from code. |
| [#294](https://github.com/taligentx/dscKeybusInterface/issues/294) | User/event question; some capabilities exist and others are not exposed structurally. |
| [#292](https://github.com/taligentx/dscKeybusInterface/issues/292) | F/A/P codes across versions; retain as a replay and firmware-validation requirement. Not physically tested in this review. |
| [#291](https://github.com/taligentx/dscKeybusInterface/issues/291) | Header-only contribution without a body or usable patch; no concrete change to import from the entry. |
| [#289](https://github.com/taligentx/dscKeybusInterface/issues/289) | Module capture lost in the Reader split; relevant and confirmed by the diff. Preserve optional capture in one controller. |
| [#264](https://github.com/taligentx/dscKeybusInterface/issues/264) | MQTT/HA configuration help; integration. |
| [#257](https://github.com/taligentx/dscKeybusInterface/issues/257) | Codes sent when using a physical keypad; maintainer asks to review MQTT/HA feedback. No core cause confirmed. |
| [#256](https://github.com/taligentx/dscKeybusInterface/issues/256) | Spurious 0x05 states without checksum; capture traces and test filtering instead of copying an arbitrary delay. |
| [#217](https://github.com/taligentx/dscKeybusInterface/issues/217) | Binary 0xD0 exchange documentation; reference for direct clock synchronization. |
| [#200](https://github.com/taligentx/dscKeybusInterface/issues/200) | DSC keypad without a panel; another mode. |
| [#196](https://github.com/taligentx/dscKeybusInterface/issues/196) | Classic tracking; outside target. |
| [#188](https://github.com/taligentx/dscKeybusInterface/issues/188) | RF5132 0x8D/0x94 programming; knowledge included in #283/develop. |
| [#183](https://github.com/taligentx/dscKeybusInterface/issues/183) | Tasmota; future adapter not required. |
| [#175](https://github.com/taligentx/dscKeybusInterface/issues/175) | Event-history decoding; useful for tests and future typed events. |
| [#166](https://github.com/taligentx/dscKeybusInterface/issues/166) | Documentation for 0x16, users, and modules; some parts implemented, not all metadata exposed. |
| [#162](https://github.com/taligentx/dscKeybusInterface/issues/162) | Module data/0x41/0xE6; reason to retain module capture and expand tests. |
| [#158](https://github.com/taligentx/dscKeybusInterface/issues/158) | PC5005 transient messages; do not extrapolate to PC1864 without evidence. |
| [#64](https://github.com/taligentx/dscKeybusInterface/issues/64) | Expanders; comments point to #290, which became obsolete. Review Dilbert's current implementation before an extension. |

## What a closed issue means

| Cases | Evidence and interpretation |
| --- | --- |
| [#272](https://github.com/taligentx/dscKeybusInterface/issues/272) | Maintainer reproduced and fixed no-entry mode on multiple partitions; user confirmed. A real fix predating the reviewed master. |
| [#131](https://github.com/taligentx/dscKeybusInterface/issues/131) | `redundantPanelData` needed IRAM. The maintainer said it landed in 2.0; master has IRAM attributes. |
| [#31](https://github.com/taligentx/dscKeybusInterface/issues/31), [#35](https://github.com/taligentx/dscKeybusInterface/issues/35), [#53](https://github.com/taligentx/dscKeybusInterface/issues/53) | Historical zone-index bugs already fixed; tests cover all eight groups and restoration. |
| [#77](https://github.com/taligentx/dscKeybusInterface/issues/77), [#87](https://github.com/taligentx/dscKeybusInterface/issues/87) | `stop` added/improved; user confirmed OTA. Our scenario verifies capture stops, not physical OTA. |
| [#100](https://github.com/taligentx/dscKeybusInterface/issues/100) | Buffer lifetime and blocking writes; changes exist in master. Not the same specific `writeReady` staleness issue. |
| [#134](https://github.com/taligentx/dscKeybusInterface/issues/134) | MQTT publication after exit cancellation fixed in integration, not a new protocol. |
| [#288](https://github.com/taligentx/dscKeybusInterface/issues/288) | User rebuilt hardware; closure does not prove a core patch. |
| [#267](https://github.com/taligentx/dscKeybusInterface/issues/267) | Workaround stops/restarts the bus around preference writes; flash/ISR and hardware interaction require consideration. |
| [#351](https://github.com/taligentx/dscKeybusInterface/issues/351) | Workaround enters/exits programming to force data; do not make it an automatic startup action. It was not executed here. |
| [#185](https://github.com/taligentx/dscKeybusInterface/issues/185) | Closed after it worked again without an identified cause. This does not prove a fix. |

## Recommendations at review time

1. Keep master intact as a reproducible reference; do not replace it based on age.
2. Use develop as a second technical reference and retain protocol knowledge,
   including information only present in diagnostics.
3. Separate diagnostics from capture in our core, keeping one capture owner and
   optional module support; avoid repeating the loss of capability in #289.
4. Prioritize explicit initialization, ownership, bounded queues, buffer lifetime,
   reliable completion, and timeouts. Dilbert's circular queue is design input,
   not an implementation to copy without reviewing its limits.
5. Migrate timers to the target SDK, compiling against 2.x/3.x when both are
   intended to be supported, with later hardware timing tests. #358/#368 are
   references, not patches ready to merge.
6. Do not import all of #339: preserve the minute-00 regression and design a
   correct distinction between observed panel clock and event date.
7. Keep MQTT, Telegram, web, user codes, and dashboards outside the core.

## Reproduce the comparison

```powershell
py tools/review_upstream.py --details
.\.venv\Scripts\python.exe tools/run_tests.py --compiler zig
.\.venv\Scripts\python.exe tools/run_tests.py --compiler zig --source-dir build/upstream-review/develop/src --build-dir build/upstream-review/develop-host --without-diagnostics
```

The last option excludes only diagnostics removed from develop's normal class;
it is not presented as an identical full suite. `tests/reader_probe.cpp` tests the
develop Reader separately.
