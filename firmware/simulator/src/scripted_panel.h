// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "keybus.h"
#ifdef DSC_ENABLE_REPLAY
#include <array>
namespace dsc::bench {
// Deliberately limited test responder, NOT an independent decoder or a complete
// DSC emulator. Every response is a raw frame processed by the production core.
class ScriptedPanel {
 public:
  enum class Behavior { Normal, NoConfirmation, RejectCode, MissingStarAck };
  explicit ScriptedPanel(Keybus &bus) : bus_(bus) {}
  bool begin();
  void step(std::uint32_t now);
  void set_behavior(Behavior behavior) { behavior_=behavior; }
  void set_exit_delay(std::uint32_t milliseconds) { exit_delay_ms_=milliseconds; }
  void set_online(bool online) { online_=online; }
  void set_zone(std::uint8_t zone, bool open);
  void set_trouble(bool trouble) { trouble_=trouble; }
  void set_fire(bool fire) { fire_=fire; }
  void enable_partition(std::uint8_t partition);
  void reset_trace() { trace_size_=0; }
  unsigned trace_size() const { return trace_size_; }
  char trace(unsigned index) const { return index<trace_size_ ? trace_[index] : 0; }
 private:
  Keybus &bus_;
  Behavior behavior_{Behavior::Normal};
  bool online_{true}, trouble_{false}, fire_{false};
  std::array<std::uint8_t,8> zones_{};
  std::array<std::uint8_t,8> message_{{1,0xC7,0xC7,0xC7,0xC7,0xC7,0xC7,0xC7}};
  std::array<bool,8> resume_exit_{};
  std::array<char,8> armed_mode_{}, pending_arm_{};
  std::array<std::uint32_t,8> arm_start_{};
  std::array<std::array<char,7>,8> entered_code_{};
  std::array<std::uint8_t,8> code_size_{};
  std::array<bool,8> menu_{};
  std::array<std::array<char,33>,8> menu_keys_{};
  std::array<std::uint8_t,8> menu_size_{};
  std::array<char,256> trace_{};
  unsigned trace_size_{0};
  std::uint32_t last_frame_{0};
  std::uint32_t exit_delay_ms_{500};
  void emit(Frame frame, bool checksum=false);
  void emit_status();
  void emit_zones();
  void key(char key, std::uint8_t partition, std::uint32_t now);
  void close_menu(unsigned index);
};
}  // namespace dsc::bench
#endif
