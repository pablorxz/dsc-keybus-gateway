// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsc_entities.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstdio>

namespace esphome::dsc_alarm_panel {
static const char *const TAG="dsc_entity";
#ifdef USE_ALARM_CONTROL_PANEL
using namespace alarm_control_panel;
// ESPHome's native alarm message has no unknown/missing-state field. Its API
// client converts unrecognized enum values to None; Home Assistant maps None to
// unknown. This sentinel must be covered by a real API/HA regression test.
// Keep the sentinel below 128: ESPHome's optimized enum encoder allocates a
// single protobuf varint byte for this field. Values >=128 produce a malformed
// packet even though the underlying C++ enum accepts them.
static constexpr auto UNKNOWN = static_cast<AlarmControlPanelState>(10);
DscPartition::DscPartition() { current_state_=UNKNOWN; }
std::uint32_t DscPartition::get_supported_features() const {
  return ACP_FEAT_ARM_HOME | ACP_FEAT_ARM_AWAY | ACP_FEAT_ARM_NIGHT;
}
void DscPartition::loop() {
  const auto now=millis();
  if (!parent_ || static_cast<std::uint32_t>(now-last_check_)<100) return;
  last_check_=now;
  using dsc::app::AlarmState;
  AlarmControlPanelState state=UNKNOWN;
  switch(parent_->service().partition(partition_).state) {
    case AlarmState::Disarmed: state=ACP_STATE_DISARMED; break;
    case AlarmState::Arming: state=ACP_STATE_ARMING; break;
    case AlarmState::Pending: state=ACP_STATE_PENDING; break;
    case AlarmState::ArmedStay: state=ACP_STATE_ARMED_HOME; break;
    case AlarmState::ArmedAway: state=ACP_STATE_ARMED_AWAY; break;
    case AlarmState::ArmedNight: state=ACP_STATE_ARMED_NIGHT; break;
    case AlarmState::Triggered: state=ACP_STATE_TRIGGERED; break;
    default: break;
  }
  if (state!=get_state()) publish_state(state);
}
void DscPartition::control(const AlarmControlPanelCall &call) {
  if (!parent_ || !call.get_state().has_value()) return;
  dsc::app::Command command; command.partition=partition_;
  switch (*call.get_state()) {
    case ACP_STATE_DISARMED: command.kind=dsc::app::CommandKind::Disarm; break;
    case ACP_STATE_ARMED_HOME: command.kind=dsc::app::CommandKind::ArmStay; break;
    case ACP_STATE_ARMED_AWAY: command.kind=dsc::app::CommandKind::ArmAway; break;
    case ACP_STATE_ARMED_NIGHT: command.kind=dsc::app::CommandKind::ArmNight; break;
    default: return;
  }
  if (call.get_code().has_value()) {
    const auto &code=*call.get_code();
    if (code.size()>=command.code.size()) { ESP_LOGW(TAG,"Command rejected: invalid code length"); return; }
    std::copy(code.begin(),code.end(),command.code.begin());
  }
  const auto result=parent_->service().submit(command,dsc::app::CommandSource::HomeAssistant,millis());
  volatile char *code=command.code.data();
  for (unsigned i=0;i<command.code.size();++i) code[i]=0;
  if (!result.id) ESP_LOGW(TAG,"Command rejected: %s",dsc::app::operation_reason_name(result.reason));
}
#endif
#ifdef USE_BINARY_SENSOR
void DscBinarySensor::loop() {
  const auto now=millis();
  if (!parent_ || static_cast<std::uint32_t>(now-last_check_)<100) return;
  last_check_=now;
  const auto state=parent_->service().snapshot();
  if (kind_==0) { publish_state(state.connected); return; }
  if (!state.connected) { if (has_state()) invalidate_state(); return; }
  if (kind_==1) { if (!state.trouble_known) { if (has_state()) invalidate_state(); return; } publish_state(state.trouble); return; }
  if (kind_==2) {
    if (!state.partitions[0].fire_known) { if (has_state()) invalidate_state(); return; }
    publish_state(state.partitions[0].fire); return;
  }
  if (kind_==3 && zone_>=1 && zone_<=64) {
    const unsigned index=zone_-1, group=index/8;
    if (!(state.known_zone_groups & (1U<<group))) { if (has_state()) invalidate_state(); return; }
    publish_state((state.open_zones[group] & (1U<<(index%8)))!=0);
  }
}
#endif
#ifdef USE_TEXT_SENSOR
void DscOperationSensor::loop() {
  const auto now=millis();
  if (!parent_ || static_cast<std::uint32_t>(now-last_check_)<100) return;
  last_check_=now;
  const auto operation=parent_->service().latest();
  char value[96];
  std::snprintf(value,sizeof value,"%u:%s:%s",unsigned(operation.id),
                dsc::app::operation_status_name(operation.status),
                dsc::app::operation_reason_name(operation.reason));
  if (!has_state() || state!=value) publish_state(value);
}
#endif
}  // namespace esphome::dsc_alarm_panel
