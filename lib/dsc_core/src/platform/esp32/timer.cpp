// SPDX-License-Identifier: GPL-3.0-or-later
// ESP32 sample timer for the taligentx-derived capture implementation.
#include "keybus.h"
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

namespace dsc {
void Keybus::setupTimer() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  timer1 = timerBegin(1000000);
  if (timer1 == nullptr) return;
  timerStop(timer1);
  timerAttachInterrupt(timer1, &dscDataInterrupt);
  timerAlarm(timer1, 250, true, 0);
#else
  timer1 = timerBegin(1, 80, true);
  if (timer1 == nullptr) return;
  timerStop(timer1);
  timerAttachInterrupt(timer1, &dscDataInterrupt, true);
  timerAlarmWrite(timer1, 250, true);
  timerAlarmEnable(timer1);
#endif
}

void Keybus::stopTimer() {
  if (timer1 == nullptr) return;
  timerStop(timer1);
#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
  timerAlarmDisable(timer1);
#endif
  timerEnd(timer1);
  timer1 = nullptr;
}

void IRAM_ATTR Keybus::armSampleTimer() {
  timerWrite(timer1, 0);
  timerStart(timer1);
}
void IRAM_ATTR Keybus::stopSampleTimer() { timerStop(timer1); }
void IRAM_ATTR Keybus::driveData(bool sink) {
  digitalWrite(dscWritePin, (sharedDataPin_ ? !sink : sink) ? HIGH : LOW);
}
}  // namespace dsc
