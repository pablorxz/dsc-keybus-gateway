// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "state.h"
#include <array>
#include <cstdint>

namespace dsc::app {
enum class ActivityKind : std::uint8_t { Connected, Disconnected, ZoneOpen, ZoneClosed, Armed, Disarmed, Trouble, TroubleCleared, Alarm, AlarmCleared, Fire, FireCleared };
struct ActivityEvent { ActivityKind kind{}; std::uint8_t number{0}; std::uint64_t time_ms{0}; };
struct ActivitySnapshot { std::array<ActivityEvent,64> events{}; std::size_t count{0}; std::uint64_t now_ms{0}; };
class ActivityLog {
 public:
  void observe(const PanelState &state,std::uint32_t now);
  void copy_to(ActivitySnapshot &result) const;
 private:
  void add(ActivityKind kind,std::uint8_t number);
  PanelState previous_{};
  std::array<ActivityEvent,64> events_{};
  std::size_t head_{0},count_{0};
  std::uint64_t elapsed_{0};
  std::uint32_t last_tick_{0};
  bool initialized_{false};
};
const char *activity_kind_name(ActivityKind kind);
}  // namespace dsc::app
