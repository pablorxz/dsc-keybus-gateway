// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "keybus.h"
#include "command_scheduler.h"
#include "zone_catalog.h"
#include "activity.h"
#include <cstdint>
#include <mutex>

namespace dsc::app {
enum class AlarmState : std::uint8_t {
  Unavailable, Disarmed, Arming, Pending, ArmedStay, ArmedAway, ArmedNight, Triggered
};
struct PartitionView {
  AlarmState state{AlarmState::Unavailable};
  bool ready{false};
  bool fire{false};
  bool disabled{false};
};
PartitionView describe_partition(const PanelState &observed, std::uint8_t number);

// Thread-safe adapter boundary. Only tick(), called by the firmware loop owner,
// touches the core. Network/native API callers submit into one shared scheduler.
class AlarmService {
 public:
  explicit AlarmService(Keybus &bus) : bus_(bus) {}
  PartitionView partition(std::uint8_t number) const;
  void tick(std::uint32_t now);
  Submission submit(const Command &command, CommandSource source, std::uint32_t now);
  OperationReason provide_code(std::uint32_t id, const std::array<char,7> &code, std::uint32_t now);
  OperationReason abandon_code(std::uint32_t id);
  OperationView operation(std::uint32_t id) const;
  OperationView latest() const;
  PanelState snapshot() const;
  bool configure_zone(std::uint8_t number, const char *name, const char *group, const char *category);
  ZoneCatalog zone_catalog() const;
  void copy_zone_catalog(ZoneCatalog &destination) const;
  void copy_activity(ActivitySnapshot &destination) const;
 private:
  Keybus &bus_;
  mutable std::mutex mutex_;
  PanelState observed_{};
  CommandScheduler scheduler_{};
  ZoneCatalog catalog_{};
  ActivityLog activity_{};
};
const char *state_name(AlarmState state);
}  // namespace dsc::app
