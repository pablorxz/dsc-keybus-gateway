// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/component.h"
#include "alarm_service.h"
#include <array>
#include <atomic>
#ifdef DSC_ENABLE_REPLAY
#include "scripted_panel.h"
#endif

namespace esphome::dsc_alarm_panel {
class DscAlarmPanel : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_simulation(bool enabled) { simulation_=enabled; }
  void set_physical_bus(bool enabled) { physical_bus_=enabled; }
  void set_pins(std::uint8_t clock, std::uint8_t read, std::uint8_t write) {
    clock_pin_=clock; read_pin_=read; write_pin_=write;
  }
  dsc::app::AlarmService &service() { return service_; }
  void register_zone(std::uint8_t number, const char *name, const char *group, const char *category);
#ifdef DSC_ENABLE_REPLAY
  dsc::bench::ScriptedPanel &simulator() { return simulator_; }
  bool schedule_simulated_zone(std::uint8_t zone, bool open);
  bool schedule_simulated_online(bool online);
#endif
 protected:
  // Simulation never starts hardware GPIOs; the physical path is opt-in.
  dsc::Keybus bus_{22, 21, 255};
  dsc::app::AlarmService service_{bus_};
  bool simulation_{false}, physical_bus_{false}, network_reported_{false};
  std::uint8_t clock_pin_{22}, read_pin_{21}, write_pin_{255};
#ifdef DSC_ENABLE_REPLAY
  dsc::bench::ScriptedPanel simulator_{bus_};
  std::array<std::atomic<std::uint8_t>,64> pending_zones_{};
  std::atomic<std::uint8_t> pending_online_{0};
  std::uint32_t last_metrics_{0};
#endif
};
}  // namespace esphome::dsc_alarm_panel
