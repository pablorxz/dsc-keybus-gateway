# Isolated ESP32 bench

These opt-in tools are for a specifically identified ESP32 that the owner has
confirmed is physically disconnected from the alarm and Keybus. CI and ordinary
host tests never run them. An address, COM port, or sample manifest alone does not
establish that a board is isolated or authorize a flash. Do not use the installed
alarm's ESP or its network settings as a fallback.

1. Create an ignored `.local/bench/device.json` using
   `firmware/bench-device.example.json` as a shape. Replace the placeholder MAC,
   serial port and explicit IP with the identified bench device. Set
   `alarm_disconnected` to true only after the physical isolation is confirmed.
   Add only authorized actions from `identify`, `serial`, `upload`, `ota`, and
   `network_test` to `authorized_actions`. The example authorizes nothing.
2. Put the bench-only Wi-Fi values in ignored `.local/bench/secrets.yaml` as
   `wifi_ssid` and `wifi_password`. Install the pinned `requirements/firmware.txt`
   in a separate virtual environment. `python tools/prepare_bench.py` retains those
   values and generates separate API and web credentials if absent.
3. Compile with `python -m esphome compile .local/bench/bench.yaml`. The rendered
   configuration starts the replay model, not the physical GPIO transport.
4. For an explicitly authorized serial upload, run `python tools/bench.py upload
   --expected-mac <confirmed-bench-mac>`. The helper checks the ignored manifest,
   rejects stale firmware and reads the bootloader MAC before flashing. Its
   `identify` and `observe` actions provide separate opt-in USB checks.
5. For authorized OTA, run `python tools/bench.py ota --expected-mac
   <confirmed-bench-mac>`. It authenticates the encrypted native API and verifies
   the device identity at the single explicit address before the update. No network
   scan or implicit address fallback is used.
6. `tools/bench_web.py` and `tools/ha_sandbox_test.py` test synthetic operations;
   the latter requires a separately configured disposable local HA instance. The
   opt-in `tools/bench_soak.py` monitors 45 minutes and records its predefined
   limits and measurements in `.local/bench/soak.log`. Do not reboot or flash the
   device during a soak, and do not report an interrupted run as a pass. Once it
   finishes, `python tools/analyze_soak.py` independently checks the completed
   log and reports its memory, HTTP and stack measurements. Its parser is tested
   without hardware by `python tests/bench/test_analyze_soak.py`. A separate
   ignored `soak-diagnostics.log` records only redacted reset and Wi-Fi event
   categories, including lines buffered while an HTTP check times out.
   When the device is intermittently unreachable, the opt-in
   `tools/bench_connectivity_watch.py --expected-mac <confirmed-bench-mac>` checks
   only the explicit bench address and requires three successive encrypted identity
   checks. It does not alter the 45-minute soak's pass criteria.

All `.local` credentials, manifests and logs are excluded from Git. The simulator
does not test real Keybus electrical levels, physical keypad coexistence or panel
timing. HTTP uses Digest authentication without TLS; this is a test interface, not
a secure production transport.
