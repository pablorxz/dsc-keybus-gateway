// SPDX-License-Identifier: GPL-3.0-or-later
#include "alarm_service.h"
#include "arming.h"

namespace dsc::app {
PartitionView describe_partition(const PanelState &observed, std::uint8_t number) {
  PartitionView result;
  if (number == 0 || number > dscPartitions) return result;
  const auto index = number - 1;
  const auto &p=observed.partitions[index];
  result.disabled = p.disabled;
  if (!observed.connected || result.disabled || !p.known) return result;
  result.ready = p.ready;
  result.fire = p.fire;
  if (p.alarm || p.fire) result.state = AlarmState::Triggered;
  else if (p.entry_delay) result.state = AlarmState::Pending;
  else if (p.exit_delay) result.state = AlarmState::Arming;
  else if (p.armed) {
    if (p.no_entry_delay) result.state = AlarmState::ArmedNight;
    else if (p.stay) result.state = AlarmState::ArmedStay;
    else if (p.away) result.state = AlarmState::ArmedAway;
    // An armed status without a decoded mode is not published as disarmed.
  } else result.state = AlarmState::Disarmed;
  return result;
}
PartitionView AlarmService::partition(std::uint8_t number) const {
  std::lock_guard<std::mutex> lock(mutex_); return describe_partition(observed_,number);
}
void AlarmService::tick(std::uint32_t now) {
  std::lock_guard<std::mutex> lock(mutex_);
  observed_=bus_.snapshot();
  activity_.observe(observed_,now);
  scheduler_.tick(bus_,observed_,now);
}
Submission AlarmService::submit(const Command &command, CommandSource source, std::uint32_t now) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto reason=validate_command(command,observed_);
  if(reason!=OperationReason::None) return {0,reason};
  return scheduler_.enqueue(command,source,now,observed_.partitions[command.partition-1].exit_delay);
}
OperationView AlarmService::operation(std::uint32_t id) const { std::lock_guard<std::mutex> lock(mutex_); return scheduler_.operation(id); }
OperationReason AlarmService::provide_code(std::uint32_t id, const std::array<char,7> &code, std::uint32_t now) { std::lock_guard<std::mutex> lock(mutex_); return scheduler_.provide_code(id,code,observed_,now); }
OperationReason AlarmService::abandon_code(std::uint32_t id) { std::lock_guard<std::mutex> lock(mutex_); return scheduler_.abandon_code(id); }
OperationView AlarmService::latest() const { std::lock_guard<std::mutex> lock(mutex_); return scheduler_.latest(); }
PanelState AlarmService::snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return observed_; }
bool AlarmService::configure_zone(std::uint8_t number,const char *name,const char *group,const char *category) {
  std::lock_guard<std::mutex> lock(mutex_); return catalog_.add(number,name,group,category);
}
ZoneCatalog AlarmService::zone_catalog() const { std::lock_guard<std::mutex> lock(mutex_); return catalog_; }
void AlarmService::copy_zone_catalog(ZoneCatalog &destination) const {
  std::lock_guard<std::mutex> lock(mutex_); destination=catalog_;
}
void AlarmService::copy_activity(ActivitySnapshot &destination) const {
  std::lock_guard<std::mutex> lock(mutex_); activity_.copy_to(destination);
}
const char *state_name(AlarmState state) {
  switch (state) {
    case AlarmState::Disarmed: return "disarmed";
    case AlarmState::Arming: return "arming";
    case AlarmState::Pending: return "pending";
    case AlarmState::ArmedStay: return "armed_home";
    case AlarmState::ArmedAway: return "armed_away";
    case AlarmState::ArmedNight: return "armed_night";
    case AlarmState::Triggered: return "triggered";
    default: return "unavailable";
  }
}
}  // namespace dsc::app
