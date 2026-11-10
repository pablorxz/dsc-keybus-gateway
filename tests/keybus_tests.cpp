#include "fake_arduino.h"
#include "powerseries.h"
// This public upstream header owns static storage definitions and is included
// in exactly one translation unit, as required by the original library.
#if defined(DSC_OWN_CORE)
#include "keybus.h"
using dscKeybusInterface = dsc::Keybus;
#else
#include "dscKeybusInterface.h"
#endif
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#if defined(DSC_OWN_CORE)
std::map<std::string, std::function<void()>> core_regressions();
#endif

namespace {
// Static storage reproduces Arduino sketch initialization (including zero-init).
dscKeybusInterface &panel(bool read_only = false) {
  static dscKeybusInterface instance(22, 21, read_only ? 255 : 18);
  return instance;
}
#define dsc panel()
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void feed(const std::vector<byte> &bytes) { fake::frame(bytes); dsc.loop(); }
void prime() { dsc.begin(); feed(fixtures::status()); check(dsc.ready[0], "Primer did not produce ready partition"); }

void ready_state() {
  prime();
  check(dsc.keybusConnected, "Keybus not connected");
  check(!dsc.armed[0] && !dsc.alarm[0], "Ready state unexpectedly armed/triggered");
  check(dsc.disabled[1] && dsc.disabled[2] && dsc.disabled[3], "Disabled partitions lost");
  check(dsc.status[0] == 0x01, "Decoded status differs");
}
void state_transitions() {
  prime();
  feed(fixtures::status(0x08));
  check(dsc.exitDelay[0], "Exit delay not detected");
  feed(fixtures::status(0x05, 0x82));
  check(dsc.armed[0] && dsc.armedAway[0] && !dsc.exitDelay[0], "Away state not decoded");
  feed(fixtures::status(0x0C, 0x82));
  check(dsc.entryDelay[0], "Entry delay not decoded");
  feed(fixtures::status(0x11, 0x82));
  check(dsc.alarm[0], "Alarm state not decoded");
  feed(fixtures::status(0x3E));
  check(!dsc.armed[0] && !dsc.alarm[0] && !dsc.entryDelay[0], "Disarmed state not decoded");
}
void event_at_zero_minute() {
  prime();
  feed(fixtures::status(0x05, 0x82));
  check(dsc.armed[0], "Fixture did not arm the simulated partition");
  // Documented 0xEB layout: partition 1, 2018-06-04 10:00, status set 0,
  // event 0xE2 = disarmed by master. Bytes 6 and 7 are both legitimately zero.
  feed(fake::with_checksum({0xEB, 0, 1, 0x18, 0x18, 0x8A, 0, 0, 0xE2, 0}));
  check(!dsc.armed[0], "A real disarm event was mistaken for a timestamp-only packet");
}
void fire_and_trouble() {
  prime();
  feed(fixtures::status(0x01, 0xD1));
  check(dsc.trouble && dsc.fire[0], "Trouble/fire flags not decoded");
  feed(fixtures::status());
  check(!dsc.trouble && !dsc.fire[0], "Trouble/fire restoration not decoded");
}
void zone_group(unsigned group) {
  prime();
  feed(fixtures::zones(group, 0x81));
  check(dsc.openZones[group] == 0x81, "First/last zone in group not opened");
  check(dsc.openZonesChanged[group] == 0x81, "Zone change mask differs");
  dsc.openZonesChanged[group] = 0;
  feed(fixtures::zones(group, 0));
  check(dsc.openZones[group] == 0, "Zones not restored");
  check(dsc.openZonesChanged[group] == 0x81, "Zone restore change mask differs");
}
void corrupt_checksum() {
  prime();
  auto packet = fixtures::zones(1, 0xFF);
  packet.back() ^= 0x01;
  feed(packet);
  check(dsc.openZones[1] == 0, "Corrupt packet modified zone state");
  feed(fixtures::zones(1, 0xFF));
  check(dsc.openZones[1] == 0xFF, "Valid packet after corruption not decoded");
}
void duplicate_frame() {
  prime();
  dsc.statusChanged = false;
  dsc.readyChanged[0] = false;
  fake::frame(fixtures::status());
  check(!dsc.loop(), "Duplicate periodic packet was not suppressed");
  check(!dsc.statusChanged && !dsc.readyChanged[0], "Duplicate packet generated a state change");
}
void timeout_and_reconnect() {
  prime();
  fake::advance(3001000);
  dsc.loop();
  check(!dsc.keybusConnected && dsc.keybusChanged, "Disconnect not detected after 3s");
  feed(fixtures::status(0x03));
  check(dsc.keybusConnected && !dsc.ready[0], "Reconnect did not refresh state");
}
void buffer_overflow() {
  prime();
  for (unsigned i = 0; i < 52; ++i) fake::frame(fixtures::status(i % 2 ? 0x01 : 0x03));
  check(dsc.bufferOverflow, "Receive buffer overflow not reported");
  for (unsigned i = 0; i < 60; ++i) dsc.loop();
  feed(fixtures::status(0x04, 0x82));
  check(dsc.armedStay[0], "Receive processing did not recover after buffer drain");
}
void read_only_mode() {
  auto &reader = panel(true);
  reader.begin();
  fake::frame(fixtures::status());
  reader.loop();
  check(reader.ready[0], "Read-only interface did not decode");
  check(fake::output(18) == LOW, "Read-only interface drove write GPIO");
}
void stop_capture() {
  prime();
  dsc.stop();
  check(!fake::attached(22), "Clock interrupt still attached after stop");
  fake::frame(fixtures::status(0x05));
  dsc.loop();
  check(!dsc.armed[0], "Stopped interface still accepted frame");
}
void transmit(char key, byte expected, unsigned partition = 1) {
  prime();
  if (partition != 1) {
    auto packet = fixtures::status();
    packet[4] = 0x81; packet[5] = 0x01;
    feed(packet);
    dsc.writePartition = static_cast<byte>(partition);
  }
  dsc.write(key);
  const auto result = fake::frame(fixtures::status());
  dsc.loop();
  check(fake::transmitted_byte(result, partition == 1 ? 8 : 16) == expected,
        "GPIO waveform does not encode requested key in correct partition slot");
  // Original loop() only refreshes writeReady while processing buffered data.
  // A repeated 0x05 packet can be filtered before that point. Provide an actual
  // status change before asserting the public readiness flag.
  feed(fixtures::status(0x03));
  check(dsc.writeReady, "Transport did not report ready after transmission");
}
void known_write_ready_staleness() {
  prime();
  dsc.write('w');
  const auto result = fake::frame(fixtures::status());
  dsc.loop();
  check(fake::transmitted_byte(result, 8) == 0xB1, "Key not physically emitted by simulated driver");
#if defined(DSC_OWN_CORE)
  check(dsc.writeReady, "Owned core did not refresh readiness after a duplicate status frame");
#else
  check(!dsc.writeReady, "Baseline characterization changed: review stale writeReady issue");
#endif
  feed(fixtures::status(0x03));
  check(dsc.writeReady, "writeReady not refreshed after a new status packet");
}
void star_handshake() {
  prime();
  dsc.write("*2");
  const auto star = fake::frame(fixtures::status());
  dsc.loop();
  check(fake::transmitted_byte(star, 8) == 0x28, "Star key not emitted");
  const auto waiting = fake::frame(fixtures::status());
  dsc.loop();
  check(fake::transmitted_byte(waiting, 8) == 0xFF, "Second key emitted before menu acknowledgement");
  feed(fixtures::status(0x9E));
  dsc.loop();
  const auto next = fake::frame(fixtures::status(0x9E));
  dsc.loop();
  check(fake::transmitted_byte(next, 8) == 0x0A, "Second key not emitted after menu acknowledgement");
}
#ifndef KEYBUS_NO_DIAGNOSTICS
void diagnostic_output() {
  prime();
  Serial.output.clear();
  dsc.printPanelCommand();
  check(Serial.output == "0x05", "Diagnostic command output differs");
  Serial.output.clear();
  dsc.printPanelMessage();
  check(Serial.output.find("Ready") != std::string::npos, "Diagnostic did not describe ready state");
}
#endif

std::map<std::string, std::function<void()>> scenarios() {
  std::map<std::string, std::function<void()>> cases = {
    {"ready-state", ready_state}, {"state-transitions", state_transitions},
    {"event-at-zero-minute", event_at_zero_minute},
    {"fire-and-trouble", fire_and_trouble}, {"corrupt-checksum", corrupt_checksum},
    {"duplicate-frame", duplicate_frame}, {"timeout-and-reconnect", timeout_and_reconnect},
    {"receive-buffer-overflow", buffer_overflow}, {"read-only-mode", read_only_mode},
    {"stop-capture", stop_capture}, {"star-handshake", star_handshake},
#ifndef KEYBUS_NO_DIAGNOSTICS
    {"diagnostic-output", diagnostic_output},
#endif
#if defined(DSC_OWN_CORE)
    {"fixed-write-ready-staleness", known_write_ready_staleness},
#else
    {"known-write-ready-staleness", known_write_ready_staleness},
#endif
    {"tx-partition-2", [] { transmit('w', 0xB1, 2); }},
  };
  for (unsigned group = 0; group < 8; ++group)
    cases["zone-group-" + std::to_string(group + 1)] = [group] { zone_group(group); };
  const std::pair<char, byte> keys[] = {
    {'0',0x00},{'1',0x05},{'2',0x0A},{'3',0x0F},{'4',0x11},
    {'5',0x16},{'6',0x1B},{'7',0x1C},{'8',0x22},{'9',0x27},
    {'#',0x2D},{'s',0xAF},{'w',0xB1},{'n',0xB6},{'c',0xBB},
    {'r',0xDA},{'x',0xE1},{'[',0xD5},{']',0xDA},{'{',0x70},{'}',0xEC},
    {'b',0x82},{'<',0x88},{'>',0x87},{'l',0xA5}
  };
  for (const auto &item : keys) {
    const char key = item.first; const byte expected = item.second;
    cases["tx-key-" + std::to_string(static_cast<unsigned char>(key))] = [key, expected] { transmit(key, expected); };
  }
#if defined(DSC_OWN_CORE)
  for (const auto &test : core_regressions()) cases.insert(test);
#endif
  return cases;
}
}

int main(int argc, char **argv) {
  const auto cases = scenarios();
  if (argc == 2 && std::string(argv[1]) == "--list") {
    for (const auto &entry : cases) std::cout << entry.first << '\n';
    return 0;
  }
  if (argc != 2 || !cases.count(argv[1])) { std::cerr << "Provide one scenario or --list\n"; return 2; }
  try { cases.at(argv[1])(); std::cout << "PASS " << argv[1] << '\n'; return 0; }
  catch (const std::exception &error) { std::cerr << "FAIL " << argv[1] << ": " << error.what() << '\n'; return 1; }
}
