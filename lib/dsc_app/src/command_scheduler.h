// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "keybus.h"
#include "operation.h"
#include <array>
namespace dsc::app {
class CommandScheduler {
 public:
  Submission enqueue(const Command &command, CommandSource source, std::uint32_t now, bool exit_delay=false);
  OperationReason provide_code(std::uint32_t id, const std::array<char,7> &code, const PanelState &state, std::uint32_t now);
  OperationReason abandon_code(std::uint32_t id);
  void tick(Keybus &bus, const PanelState &state, std::uint32_t now);
  OperationView operation(std::uint32_t id) const;
  OperationView latest() const;
 private:
  struct Job {
    Command command{};
    OperationView view{};
    std::uint32_t submitted{0}, dispatched_version{0};
    bool code_sent{false}, dispatched{false}, priority{false};
  };
  static constexpr unsigned NORMAL_CAPACITY=4, CAPACITY=5, HISTORY=16;
  std::array<Job,CAPACITY> jobs_{};
  std::array<OperationView,HISTORY> history_{};
  unsigned head_{0}, count_{0}, history_head_{0};
  std::uint32_t next_id_{1};
  void finish(OperationStatus status, OperationReason reason);
  void supersede_queued(unsigned offset);
  void start(Keybus &bus, const PanelState &state);
};
}  // namespace dsc::app
