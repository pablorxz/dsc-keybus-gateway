// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dsc_alarm_panel.h"
#ifdef USE_ALARM_CONTROL_PANEL
#include "esphome/components/alarm_control_panel/alarm_control_panel.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome::dsc_alarm_panel {
#ifdef USE_ALARM_CONTROL_PANEL
class DscPartition : public alarm_control_panel::AlarmControlPanel, public Component {
 public:
  DscPartition();
  void set_parent(DscAlarmPanel *parent) { parent_=parent; }
  void set_partition(std::uint8_t partition) { partition_=partition; }
  void loop() override;
  std::uint32_t get_supported_features() const override;
  bool get_requires_code() const override { return true; }
  bool get_requires_code_to_arm() const override { return true; }
 protected:
  void control(const alarm_control_panel::AlarmControlPanelCall &call) override;
  DscAlarmPanel *parent_{nullptr};
  std::uint8_t partition_{1};
  std::uint32_t last_check_{0};
};
#endif
#ifdef USE_BINARY_SENSOR
class DscBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void set_parent(DscAlarmPanel *parent) { parent_=parent; }
  void set_kind(std::uint8_t kind) { kind_=kind; }
  void set_zone(std::uint8_t zone) { zone_=zone; }
  void loop() override;
 protected:
  DscAlarmPanel *parent_{nullptr};
  std::uint8_t kind_{0}, zone_{0};
  std::uint32_t last_check_{0};
};
#endif
#ifdef USE_TEXT_SENSOR
class DscOperationSensor : public text_sensor::TextSensor, public Component {
 public:
  void set_parent(DscAlarmPanel *parent) { parent_=parent; }
  void loop() override;
 protected:
  DscAlarmPanel *parent_{nullptr};
  std::uint32_t last_check_{0};
};
#endif
}  // namespace esphome::dsc_alarm_panel
