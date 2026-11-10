// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace dsc {
struct PanelClock {
  std::uint16_t year{0};
  std::uint8_t month{0}, day{0}, hour{0}, minute{0};
  bool valid{false};
};
struct PartitionState {
  bool known{false}, disabled{false}, ready{false}, armed{false};
  bool stay{false}, away{false}, no_entry_delay{false};
  bool alarm{false}, fire{false}, entry_delay{false}, exit_delay{false}, fire_known{false};
  std::uint8_t status{0}, lights{0};
};
struct PanelState {
  std::uint32_t version{0};
  // Last sampled clock activity, in device monotonic milliseconds. Valid only
  // while connected; it is not a validated panel message or a heartbeat.
  std::uint32_t bus_activity_ms{0};
  bool connected{false}, simulated{false};
  bool trouble{false}, power_trouble{false}, battery_trouble{false}, trouble_known{false};
  std::uint8_t known_zone_groups{0};
  std::array<PartitionState, 8> partitions{};
  std::array<std::uint8_t, 8> open_zones{}, alarm_zones{};
  PanelClock panel_clock{}, event_clock{};
};
}  // namespace dsc
