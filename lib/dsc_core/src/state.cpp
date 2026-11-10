// SPDX-License-Identifier: GPL-3.0-or-later
#include "keybus.h"

namespace dsc {
PanelState Keybus::snapshot() const {
  PanelState state;
  state.version = stateVersion_;
  state.connected = keybusConnected && started_;
  #if defined(ESP32)
  portENTER_CRITICAL(&timer1Mux);
  #else
  noInterrupts();
  #endif
  state.bus_activity_ms = static_cast<std::uint32_t>(keybusTime);
  #if defined(ESP32)
  portEXIT_CRITICAL(&timer1Mux);
  #else
  interrupts();
  #endif
  state.simulated = replay_;
  state.trouble = trouble;
  state.trouble_known = troubleKnown_;
  state.power_trouble = powerTrouble;
  state.battery_trouble = batteryTrouble;
  state.known_zone_groups = knownZoneGroups_;
  state.panel_clock = panelClock_;
  state.event_clock = eventClock_;
  for (byte i=0; i<dscPartitions; ++i) {
    state.partitions[i] = {
      bool(knownPartitions_ & (1U << i)), disabled[i], ready[i], armed[i],
      armedStay[i], armedAway[i], noEntryDelay[i], alarm[i], fire[i],
      entryDelay[i], exitDelay[i], fireKnown_[i], status[i], lights[i]
    };
  }
  for (byte i=0; i<dscZones; ++i) {
    state.open_zones[i] = openZones[i];
    state.alarm_zones[i] = alarmZones[i];
  }
  return state;
}

bool Keybus::validFrame() const {
  if (panelByteCount < 3 || panelByteCount > FRAME_CAPACITY) return false;
  switch (panelData[0]) {
    case 0x05: return panelByteCount == 6 || panelByteCount == 10;
    case 0x1B: return panelByteCount == 10;
    case 0x16: return panelByteCount >= 6;
    case 0x27: case 0x2D: case 0x34: case 0x3E: return panelByteCount >= 8;
    case 0x87: return panelByteCount >= 5;
    case 0xA5: return panelByteCount >= 9;
    case 0xEB: return panelByteCount >= 11;
    case 0xE6: return panelData[2] == 0x1A ? panelByteCount >= 8 : panelByteCount >= 5;
    default: return false;
  }
}
}  // namespace dsc
