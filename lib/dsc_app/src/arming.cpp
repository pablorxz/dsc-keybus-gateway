// SPDX-License-Identifier: GPL-3.0-or-later
#include "arming.h"
#include "clock_sync.h"
#include "keypad_session.h"

namespace dsc::app {
bool valid_code(const std::array<char,7> &code, bool required) {
  unsigned size=0;
  while (size < code.size() && code[size]) {
    if (code[size]<'0' || code[size]>'9') return false;
    ++size;
  }
  return (!required && size==0) || size==4 || size==6;
}
bool target_matches(const Command &command, const PanelState &state) {
  if (command.partition==0 || command.partition>state.partitions.size()) return false;
  const auto &p=state.partitions[command.partition-1];
  if (!state.connected || !p.known || p.disabled) return false;
  switch (command.kind) {
    case CommandKind::ArmStay: return p.armed && p.stay && !p.no_entry_delay && !p.exit_delay;
    case CommandKind::ArmAway: return p.armed && p.away && !p.no_entry_delay && !p.exit_delay;
    case CommandKind::ArmNight: return p.armed && p.no_entry_delay && !p.exit_delay;
    case CommandKind::Disarm: return !p.armed && !p.alarm && !p.entry_delay && !p.exit_delay;
    case CommandKind::SetClock: {
      const auto &a=state.panel_clock, &b=command.clock;
      return a.valid && a.year==b.year && a.month==b.month && a.day==b.day && a.hour==b.hour && a.minute==b.minute;
    }
    default: return false;
  }
}
OperationReason validate_command(const Command &command, const PanelState &state) {
  if (command.partition==0 || command.partition>state.partitions.size()) return OperationReason::InvalidPartition;
  const auto &p=state.partitions[command.partition-1];
  if (!state.connected || !p.known || p.disabled) return OperationReason::Unavailable;
  if (command.timeout_ms<100 || command.timeout_ms>300000) return OperationReason::InvalidKeys;
  const bool code_required=command.kind==CommandKind::Disarm || command.kind==CommandKind::SetClock;
  if (code_required && command.code[0]==0) return OperationReason::CodeRequired;
  if (!valid_code(command.code,code_required)) return OperationReason::InvalidCode;
  if (command.kind==CommandKind::Keypad) return valid_keypad_sequence(command.keys) ? OperationReason::None : OperationReason::InvalidKeys;
  if (command.kind==CommandKind::SetClock && !valid_clock(command.clock)) return OperationReason::InvalidTime;
  if (p.status>=0x9E) return OperationReason::Busy;
  if (command.kind!=CommandKind::Disarm && command.kind!=CommandKind::SetClock &&
      (p.armed || p.exit_delay || p.entry_delay || p.alarm) && !target_matches(command,state)) return OperationReason::Busy;
  return OperationReason::None;
}
}  // namespace dsc::app
