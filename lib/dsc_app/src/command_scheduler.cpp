// SPDX-License-Identifier: GPL-3.0-or-later
#include "command_scheduler.h"
#include "arming.h"
#include "clock_sync.h"
#include <cstring>

namespace dsc::app {
OperationReason CommandScheduler::provide_code(std::uint32_t id, const std::array<char,7> &code, const PanelState &state, std::uint32_t now) {
  if (!valid_code(code,true)) return OperationReason::InvalidCode;
  if (!count_ || jobs_[head_].view.id!=id || jobs_[head_].view.source!=CommandSource::Http || jobs_[head_].view.status!=OperationStatus::AwaitingCode) return OperationReason::Busy;
  auto &job=jobs_[head_]; const auto &p=state.partitions[job.command.partition-1];
  if(static_cast<std::uint32_t>(now-job.submitted)>=job.command.timeout_ms) return OperationReason::NoConfirmation;
  if (!state.connected || !p.known || p.disabled || p.status!=0x9F) return OperationReason::Unavailable;
  job.command.code=code; job.view.status=OperationStatus::AwaitingPanel; job.view.reason=OperationReason::None;
  return OperationReason::None;
}
OperationReason CommandScheduler::abandon_code(std::uint32_t id) {
  if (!count_ || jobs_[head_].view.id!=id || jobs_[head_].view.source!=CommandSource::Http || jobs_[head_].view.status!=OperationStatus::AwaitingCode) return OperationReason::Busy;
  finish(OperationStatus::Uncertain,OperationReason::CodeRequired); return OperationReason::None;
}
Submission CommandScheduler::enqueue(const Command &command, CommandSource source, std::uint32_t now, bool exit_delay) {
  const auto is_arm=[](CommandKind kind) { return kind==CommandKind::ArmStay || kind==CommandKind::ArmAway || kind==CommandKind::ArmNight; };
  // A second queued disarm could later send the code to an already-disarmed
  // panel, where the same code may arm it instead.
  for(unsigned i=0;i<count_;++i) {
    const auto &existing=jobs_[(head_+i)%CAPACITY].command;
    if(existing.kind==CommandKind::Disarm && existing.partition==command.partition) return {0,OperationReason::Busy};
  }
  const bool urgent=exit_delay && command.kind==CommandKind::Disarm && count_ &&
                    jobs_[head_].dispatched && is_arm(jobs_[head_].command.kind) &&
                    jobs_[head_].command.partition==command.partition &&
                    (count_<2 || !jobs_[(head_+1)%CAPACITY].priority);
  // Waiting commands for this partition must not execute after the requested
  // disarm. They have not started, so record them as rejected and remove them.
  if(urgent) for(unsigned i=1;i<count_;) {
    if(jobs_[(head_+i)%CAPACITY].command.partition==command.partition) supersede_queued(i);
    else ++i;
  }
  if (count_>=CAPACITY || (!urgent && count_>=NORMAL_CAPACITY)) return {0,OperationReason::QueueFull};
  if(urgent) for(unsigned i=count_;i>1;--i) jobs_[(head_+i)%CAPACITY]=jobs_[(head_+i-1)%CAPACITY];
  auto &job=jobs_[(head_+(urgent?1:count_))%CAPACITY]; job={}; job.priority=urgent;
  job.command=command; job.submitted=now;
  job.view={next_id_++,command.kind,source,command.partition,OperationStatus::Queued,OperationReason::None};
  if (next_id_==0) ++next_id_;
  ++count_; return {job.view.id,OperationReason::None};
}
void CommandScheduler::supersede_queued(unsigned offset) {
  auto &job=jobs_[(head_+offset)%CAPACITY];
  job.view.status=OperationStatus::Rejected; job.view.reason=OperationReason::Superseded;
  history_[history_head_]=job.view; history_head_=(history_head_+1)%HISTORY;
  volatile char *code=job.command.code.data(); for(unsigned i=0;i<job.command.code.size();++i) code[i]=0;
  volatile char *keys=job.command.keys.data(); for(unsigned i=0;i<job.command.keys.size();++i) keys[i]=0;
  for(unsigned i=offset;i+1<count_;++i) jobs_[(head_+i)%CAPACITY]=jobs_[(head_+i+1)%CAPACITY];
  auto &tail=jobs_[(head_+count_-1)%CAPACITY];
  volatile char *tail_code=tail.command.code.data(); for(unsigned i=0;i<tail.command.code.size();++i) tail_code[i]=0;
  volatile char *tail_keys=tail.command.keys.data(); for(unsigned i=0;i<tail.command.keys.size();++i) tail_keys[i]=0;
  tail={}; --count_;
}
void CommandScheduler::finish(OperationStatus status, OperationReason reason) {
  auto &job=jobs_[head_]; job.view.status=status; job.view.reason=reason;
  history_[history_head_]=job.view; history_head_=(history_head_+1)%HISTORY;
  // Do not retain access codes/keypad payloads in completed operation history.
  volatile char *code=job.command.code.data(); for(unsigned i=0;i<job.command.code.size();++i) code[i]=0;
  volatile char *keys=job.command.keys.data(); for(unsigned i=0;i<job.command.keys.size();++i) keys[i]=0;
  job={}; head_=(head_+1)%CAPACITY; --count_;
}
void CommandScheduler::start(Keybus &bus, const PanelState &state) {
  auto &job=jobs_[head_];
  const auto reason=validate_command(job.command,state);
  if (reason!=OperationReason::None) { finish(OperationStatus::Rejected,reason); return; }
  if (target_matches(job.command,state)) { finish(OperationStatus::Succeeded,OperationReason::AlreadyInState); return; }
  if (bus.pendingKeys()) { finish(OperationStatus::Rejected,OperationReason::Busy); return; }
  char keys[33]{};
  switch(job.command.kind) {
    case CommandKind::ArmStay: keys[0]='s'; break;
    case CommandKind::ArmAway: keys[0]='w'; break;
    case CommandKind::ArmNight: keys[0]='n'; break;
    case CommandKind::Disarm: std::copy(job.command.code.begin(),job.command.code.end(),keys); job.code_sent=true; break;
    case CommandKind::Keypad: std::copy(job.command.keys.begin(),job.command.keys.end(),keys); break;
    case CommandKind::SetClock: format_clock_sequence(job.command,keys); break;
  }
  const auto admitted=bus.enqueue(keys,job.command.partition);
  volatile char *wipe=keys; for(unsigned i=0;i<sizeof keys;++i) wipe[i]=0;
  if (admitted!=TxAdmission::Accepted) { finish(OperationStatus::Rejected,OperationReason::TransportError); return; }
  job.dispatched_version=state.version; job.dispatched=true; job.view.status=OperationStatus::Sending;
}
void CommandScheduler::tick(Keybus &bus, const PanelState &state, std::uint32_t now) {
  if (!count_) return;
  auto &job=jobs_[head_];
  if (!state.connected) {
    const bool sent=job.dispatched;
    if (sent) bus.cancelTx(TxOutcome::Disconnected);
    finish(sent?OperationStatus::Uncertain:OperationStatus::Rejected,OperationReason::Unavailable); return;
  }
  if (static_cast<std::uint32_t>(now-job.submitted)>=job.command.timeout_ms) {
    if (job.dispatched) bus.cancelTx();
    finish(OperationStatus::TimedOut,OperationReason::NoConfirmation); return;
  }
  // Once a panel has entered exit delay, a user-requested disarm may interrupt
  // the wait for arming confirmation, but never a key sequence in flight.
  if(count_>1 && job.dispatched && !bus.pendingKeys() &&
     (job.command.kind==CommandKind::ArmStay || job.command.kind==CommandKind::ArmAway || job.command.kind==CommandKind::ArmNight)) {
    const auto &next=jobs_[(head_+1)%CAPACITY];
    if(next.command.kind==CommandKind::Disarm && next.command.partition==job.command.partition && next.priority) {
      finish(OperationStatus::Uncertain,OperationReason::Superseded); return;
    }
  }
  if (job.view.status==OperationStatus::Queued) { start(bus,state); return; }
  const auto &p=state.partitions[job.command.partition-1];
  if(job.view.status==OperationStatus::AwaitingCode && (!p.known || p.disabled || p.status!=0x9F)) { finish(OperationStatus::Uncertain,OperationReason::Unavailable); return; }
  if (p.status==0x8F && state.version!=job.dispatched_version) {
    bus.cancelTx(); finish(OperationStatus::Rejected,OperationReason::PanelRejected); return;
  }
  const auto transport=bus.txOutcome();
  if (transport==TxOutcome::TimedOut || transport==TxOutcome::Disconnected || transport==TxOutcome::Canceled) {
    finish(OperationStatus::Uncertain,OperationReason::TransportError); return;
  }
  if (job.view.status==OperationStatus::Sending && bus.pendingKeys()==0) {
    if (job.command.kind==CommandKind::Keypad) { finish(OperationStatus::Transmitted,OperationReason::None); return; }
    job.view.status=OperationStatus::AwaitingPanel;
  }
  if (job.view.status==OperationStatus::AwaitingPanel) {
    if (state.version!=job.dispatched_version && target_matches(job.command,state)) {
      finish(OperationStatus::Succeeded,OperationReason::None); return;
    }
    if ((job.command.kind==CommandKind::ArmStay || job.command.kind==CommandKind::ArmAway || job.command.kind==CommandKind::ArmNight)
        && p.status==0x9F && state.version!=job.dispatched_version && !job.code_sent) {
      if (job.command.code[0]==0) {
        if(job.view.source==CommandSource::Http) { job.view.status=OperationStatus::AwaitingCode; job.view.reason=OperationReason::CodeRequired; }
        else finish(OperationStatus::Rejected,OperationReason::CodeRequired);
        return;
      }
      if (bus.enqueue(job.command.code.data(),job.command.partition)!=TxAdmission::Accepted) {
        finish(OperationStatus::Rejected,OperationReason::TransportError); return;
      }
      job.code_sent=true; job.view.status=OperationStatus::Sending;
    }
  }
}
OperationView CommandScheduler::operation(std::uint32_t id) const {
  if (id==0) return {};
  for(unsigned i=0;i<count_;++i) if(jobs_[(head_+i)%CAPACITY].view.id==id) return jobs_[(head_+i)%CAPACITY].view;
  for(const auto &view:history_) if(view.id==id) return view;
  return {};
}
OperationView CommandScheduler::latest() const {
  if(count_) return jobs_[(head_+count_-1)%CAPACITY].view;
  return history_[(history_head_+HISTORY-1)%HISTORY];
}
const char *operation_status_name(OperationStatus s) {
  switch(s) {
    case OperationStatus::Queued:return "queued"; case OperationStatus::Sending:return "sending";
    case OperationStatus::AwaitingPanel:return "awaiting_panel"; case OperationStatus::AwaitingCode:return "awaiting_code"; case OperationStatus::Succeeded:return "succeeded";
    case OperationStatus::Transmitted:return "transmitted"; case OperationStatus::Rejected:return "rejected";
    case OperationStatus::TimedOut:return "timed_out"; case OperationStatus::Uncertain:return "uncertain";
    default:return "unknown";
  }
}
const char *operation_reason_name(OperationReason r) {
  switch(r) {
    case OperationReason::None:return "none"; case OperationReason::InvalidPartition:return "invalid_partition";
    case OperationReason::Unavailable:return "unavailable"; case OperationReason::CodeRequired:return "code_required";
    case OperationReason::InvalidCode:return "invalid_code"; case OperationReason::InvalidKeys:return "invalid_keys";
    case OperationReason::Busy:return "busy"; case OperationReason::QueueFull:return "queue_full";
    case OperationReason::TransportError:return "transport_error"; case OperationReason::NoConfirmation:return "no_confirmation";
    case OperationReason::PanelRejected:return "panel_rejected"; case OperationReason::AlreadyInState:return "already_in_state";
    case OperationReason::InvalidTime:return "invalid_time"; case OperationReason::Superseded:return "superseded"; default:return "unknown";
  }
}
const char *command_name(CommandKind k) {
  switch(k) {
    case CommandKind::ArmStay:return "arm_home"; case CommandKind::ArmAway:return "arm_away";
    case CommandKind::ArmNight:return "arm_night"; case CommandKind::Disarm:return "disarm";
    case CommandKind::Keypad:return "keypad"; case CommandKind::SetClock:return "set_clock";
    default:return "unknown";
  }
}
}  // namespace dsc::app
