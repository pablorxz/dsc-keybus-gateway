// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace dsc {
constexpr std::uint8_t FRAME_CAPACITY = 16;
struct Frame {
  // Protocol byte 1 holds the standalone stop bit, as documented by taligentx.
  std::array<std::uint8_t, FRAME_CAPACITY> bytes{};
  std::uint8_t size{0};
};
}  // namespace dsc
