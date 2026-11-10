// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace dsc {
enum class TxAdmission : std::uint8_t {
  Accepted, QueueFull, InvalidKey, InvalidPartition, Unavailable, ReadOnly, TooLong
};
enum class TxOutcome : std::uint8_t { Idle, Pending, Transmitted, TimedOut, Disconnected, Canceled };
struct TxKey {
  char key{0};
  std::uint8_t partition{1};
};
constexpr std::uint8_t TX_CAPACITY = 32;
constexpr std::uint32_t TX_KEY_TIMEOUT_MS = 5000;
}  // namespace dsc
