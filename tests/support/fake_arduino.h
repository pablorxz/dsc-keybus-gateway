#pragma once
#include "Arduino.h"
#include <vector>

namespace fake {
void advance(unsigned long microseconds);
void set_millis(std::uint32_t milliseconds);
void input(byte pin, int value);
void clock(byte pin, int value);
int output(byte pin);
bool attached(byte pin);
int mode(byte pin);
void configure_pins(byte clock, byte read, byte write, bool active_low);
void sample(bool panel_bit, std::vector<int> *wire_bits = nullptr);
struct FrameResult { std::vector<int> wire_bits; };
FrameResult frame(const std::vector<byte> &bytes);
byte transmitted_byte(const FrameResult &result, unsigned first_bit);
std::vector<byte> with_checksum(std::vector<byte> bytes);
}  // namespace fake

