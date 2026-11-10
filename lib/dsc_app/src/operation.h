// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "state.h"
#include <array>
#include <cstdint>

namespace dsc::app {
enum class CommandKind : std::uint8_t { ArmStay, ArmAway, ArmNight, Disarm, Keypad, SetClock };
enum class CommandSource : std::uint8_t { HomeAssistant, Http, Test };
enum class OperationStatus : std::uint8_t {
  Unknown, Queued, Sending, AwaitingPanel, AwaitingCode, Succeeded, Transmitted, Rejected, TimedOut, Uncertain
};
enum class OperationReason : std::uint8_t {
  None, InvalidPartition, Unavailable, CodeRequired, InvalidCode, InvalidKeys,
  Busy, QueueFull, TransportError, NoConfirmation, PanelRejected, AlreadyInState, InvalidTime, Superseded
};
struct Command {
  CommandKind kind{CommandKind::ArmAway};
  std::uint8_t partition{1};
  std::array<char,7> code{};
  std::array<char,33> keys{};
  PanelClock clock{};
  std::uint32_t timeout_ms{120000};
};
struct OperationView {
  std::uint32_t id{0};
  CommandKind kind{CommandKind::ArmAway};
  CommandSource source{CommandSource::Test};
  std::uint8_t partition{1};
  OperationStatus status{OperationStatus::Unknown};
  OperationReason reason{OperationReason::None};
  bool terminal() const {
    return status==OperationStatus::Succeeded || status==OperationStatus::Transmitted ||
           status==OperationStatus::Rejected || status==OperationStatus::TimedOut || status==OperationStatus::Uncertain;
  }
};
struct Submission { std::uint32_t id{0}; OperationReason reason{OperationReason::None}; };
const char *operation_status_name(OperationStatus status);
const char *operation_reason_name(OperationReason reason);
const char *command_name(CommandKind kind);
}  // namespace dsc::app
