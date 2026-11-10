#include "fake_arduino.h"
#include <array>
#include <stdexcept>

Stream Serial;
namespace {
std::uint64_t clock_us = 0;
std::uint64_t timer_due = 0;
std::array<int, 64> inputs{};
std::array<int, 64> outputs{};
std::array<int, 64> modes{};
std::array<void (*)(), 64> interrupts{};
hw_timer_t timer;
byte clock_pin = 22, read_pin = 21, write_pin = 18;
bool write_active_low = false;
}

unsigned long millis() { return static_cast<unsigned long>(clock_us / 1000); }
unsigned long micros() { return static_cast<unsigned long>(clock_us); }
void yield() { fake::advance(1); }
void pinMode(byte pin, int value) { modes.at(pin) = value; }
int digitalRead(byte pin) { return inputs.at(pin); }
void digitalWrite(byte pin, int value) { outputs.at(pin) = value; }
void attachInterrupt(int pin, void (*callback)(), int) { interrupts.at(pin) = callback; }
void detachInterrupt(int pin) { interrupts.at(pin) = nullptr; }
hw_timer_t *timerBegin(int, int, bool) { timer = {}; return &timer; }
void timerStop(hw_timer_t *value) { value->running = false; }
void timerStart(hw_timer_t *value) { value->running = true; timer_due = clock_us + value->interval; }
void timerWrite(hw_timer_t *, std::uint64_t) {}
void timerAttachInterrupt(hw_timer_t *value, void (*callback)(), bool) { value->callback = callback; }
void timerAlarmWrite(hw_timer_t *value, unsigned long interval, bool) { value->interval = interval; }
void timerAlarmEnable(hw_timer_t *value) { value->enabled = true; }
void timerAlarmDisable(hw_timer_t *value) { value->enabled = false; }
void timerEnd(hw_timer_t *value) { value->running = false; value->enabled = false; }
char *itoa(int value, char *buffer, int base) {
  if (base != 10) throw std::runtime_error("Host itoa supports decimal only");
  std::sprintf(buffer, "%d", value);
  return buffer;
}

namespace fake {
void set_millis(std::uint32_t milliseconds) {
  clock_us=static_cast<std::uint64_t>(milliseconds)*1000;
}
void advance(unsigned long microseconds) {
  const auto target = clock_us + microseconds;
  if (timer.enabled && timer.running && timer_due <= target) {
    clock_us = timer_due;
    timer.running = false;
    if (timer.callback) timer.callback();
  }
  clock_us = target;
}
void input(byte pin, int value) { inputs.at(pin) = value; }
void clock(byte pin, int value) {
  if (inputs.at(pin) == value) return;
  inputs.at(pin) = value;
  if (interrupts.at(pin)) interrupts.at(pin)();
}
int output(byte pin) { return outputs.at(pin); }
bool attached(byte pin) { return interrupts.at(pin) != nullptr; }
int mode(byte pin) { return modes.at(pin); }
void configure_pins(byte clock, byte read, byte write, bool active_low) {
  clock_pin=clock; read_pin=read; write_pin=write; write_active_low=active_low;
}
void sample(bool panel_bit, std::vector<int> *wire_bits) {
  input(read_pin, panel_bit);
  clock(clock_pin, HIGH);
  advance(500);
  input(read_pin, HIGH);  // Other simulated modules leave the data wire idle.
  clock(clock_pin, LOW);
  if (wire_bits) wire_bits->push_back((output(write_pin) == (write_active_low ? LOW : HIGH)) ? 0 : 1);
  advance(500);
}
FrameResult frame(const std::vector<byte> &bytes) {
  if (bytes.size() < 2 || bytes.size() > 16) throw std::runtime_error("Invalid fixture length");
  FrameResult result;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index == 1) { sample(bytes[index] != 0, &result.wire_bits); continue; }
    for (int bit = 7; bit >= 0; --bit) sample((bytes[index] >> bit) & 1, &result.wire_bits);
  }
  // The long clock-high interval delimits messages and flushes the capture buffer.
  input(read_pin, HIGH);
  clock(clock_pin, HIGH);
  advance(1500);
  clock(clock_pin, LOW);
  advance(500);
  return result;
}
byte transmitted_byte(const FrameResult &result, unsigned first_bit) {
  byte value = 0;
  for (unsigned i = 0; i < 8; ++i) value = static_cast<byte>((value << 1) | result.wire_bits.at(first_bit + i));
  return value;
}
std::vector<byte> with_checksum(std::vector<byte> bytes) {
  unsigned sum = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i) if (i != 1) sum += bytes[i];
  bytes.push_back(static_cast<byte>(sum));
  return bytes;
}
}  // namespace fake

