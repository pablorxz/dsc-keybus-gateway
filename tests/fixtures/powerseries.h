#pragma once
#include "Arduino.h"
#include "fake_arduino.h"
#include <vector>

// Synthetic fixtures derived from upstream's documented packet layouts, not
// recordings of a real panel. Byte 1 is the separately stored stop bit.
// See tests/reference-taligentx/src/dscKeybusPrintData.cpp (commands 0x05, 0x27, 0xE6).
namespace fixtures {
inline std::vector<byte> status(byte message = 0x01, byte lights = 0x81, byte command = 0x05) {
  return {command, 0, lights, message, 0x80, 0xC7, 0x80, 0xC7, 0x80, 0xC7};
}
inline std::vector<byte> zones(unsigned group, byte mask) {
  if (group < 4) {
    const byte commands[] = {0x27, 0x2D, 0x34, 0x3E};
    return fake::with_checksum({commands[group], 0, 0x81, 0x01, 0x80, 0xC7, mask});
  }
  const byte subcommands[] = {0x09, 0x0B, 0x0D, 0x0F};
  return fake::with_checksum({0xE6, 0, subcommands[group - 4], mask});
}
}  // namespace fixtures

