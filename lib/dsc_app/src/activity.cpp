// SPDX-License-Identifier: GPL-3.0-or-later
#include "activity.h"
namespace dsc::app {
void ActivityLog::add(ActivityKind kind,std::uint8_t number) {
  events_[head_]={kind,number,elapsed_}; head_=(head_+1)%events_.size();
  if(count_<events_.size()) ++count_;
}
void ActivityLog::observe(const PanelState &state,std::uint32_t now) {
  if(!initialized_) { previous_=state; last_tick_=now; initialized_=true; return; }
  elapsed_+=static_cast<std::uint32_t>(now-last_tick_); last_tick_=now;
  if(state.connected!=previous_.connected) add(state.connected?ActivityKind::Connected:ActivityKind::Disconnected,0);
  if(state.connected && previous_.connected) {
    for(unsigned group=0;group<8;++group) {
      const auto mask=std::uint8_t(1u<<group);
      if(!(state.known_zone_groups&mask) || !(previous_.known_zone_groups&mask)) continue;
      const auto changed=std::uint8_t(state.open_zones[group]^previous_.open_zones[group]);
      for(unsigned bit=0;bit<8;++bit) if(changed&(1u<<bit))
        add((state.open_zones[group]&(1u<<bit))?ActivityKind::ZoneOpen:ActivityKind::ZoneClosed,std::uint8_t(group*8+bit+1));
    }
    for(unsigned i=0;i<state.partitions.size();++i) {
      const auto &a=previous_.partitions[i], &b=state.partitions[i];
      if(a.known && b.known && !a.disabled && !b.disabled && a.armed!=b.armed)
        add(b.armed?ActivityKind::Armed:ActivityKind::Disarmed,std::uint8_t(i+1));
      if(a.known && b.known && !a.disabled && !b.disabled && a.alarm!=b.alarm)
        add(b.alarm?ActivityKind::Alarm:ActivityKind::AlarmCleared,std::uint8_t(i+1));
      if(a.fire_known && b.fire_known && !a.disabled && !b.disabled && a.fire!=b.fire)
        add(b.fire?ActivityKind::Fire:ActivityKind::FireCleared,std::uint8_t(i+1));
    }
    if(state.trouble_known && previous_.trouble_known && state.trouble!=previous_.trouble)
      add(state.trouble?ActivityKind::Trouble:ActivityKind::TroubleCleared,0);
  }
  previous_=state;
}
void ActivityLog::copy_to(ActivitySnapshot &result) const {
  result.count=count_; result.now_ms=elapsed_;
  for(std::size_t i=0;i<count_;++i) result.events[i]=events_[(head_+events_.size()-1-i)%events_.size()];
}
const char *activity_kind_name(ActivityKind kind) {
  switch(kind) {
    case ActivityKind::Connected:return "connected"; case ActivityKind::Disconnected:return "disconnected";
    case ActivityKind::ZoneOpen:return "zone_open"; case ActivityKind::ZoneClosed:return "zone_closed";
    case ActivityKind::Armed:return "armed"; case ActivityKind::Disarmed:return "disarmed";
    case ActivityKind::Trouble:return "trouble"; case ActivityKind::TroubleCleared:return "trouble_cleared";
    case ActivityKind::Alarm:return "alarm"; case ActivityKind::AlarmCleared:return "alarm_cleared";
    case ActivityKind::Fire:return "fire"; case ActivityKind::FireCleared:return "fire_cleared";
  } return "unknown";
}
}  // namespace dsc::app
