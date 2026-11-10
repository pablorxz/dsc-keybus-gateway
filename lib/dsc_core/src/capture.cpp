// Maintained derivation of taligentx/dscKeybusInterface @ ce12e1c05c1d.
// SPDX-License-Identifier: GPL-3.0-or-later
// Original attribution and license below; see docs/ports.md.
/*
    DSC Keybus Interface

    https://github.com/taligentx/dscKeybusInterface

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "keybus.h"

namespace dsc {
#if defined(__AVR__)
bool Keybus::redundantPanelData(byte previousCmd[], volatile byte currentCmd[], byte checkedBytes) {
#elif defined(ESP8266)
bool ICACHE_RAM_ATTR Keybus::redundantPanelData(byte previousCmd[], volatile byte currentCmd[], byte checkedBytes) {
#elif defined(ESP32)
bool IRAM_ATTR Keybus::redundantPanelData(byte previousCmd[], volatile byte currentCmd[], byte checkedBytes) {
#endif

  bool redundantData = true;
  for (byte i = 0; i < checkedBytes; i++) {
    if (previousCmd[i] != currentCmd[i]) {
      redundantData = false;
      break;
    }
  }
  if (redundantData) return true;
  else {
    for (byte i = 0; i < dscReadSize; i++) previousCmd[i] = currentCmd[i];
    return false;
  }
}


// Called as an interrupt when the DSC clock changes to write data for virtual keypad and setup timers to read
// data after an interval.
#if defined(__AVR__)
void Keybus::dscClockInterrupt() {
#elif defined(ESP8266)
void ICACHE_RAM_ATTR Keybus::dscClockInterrupt() {
#elif defined(ESP32)
void IRAM_ATTR Keybus::dscClockInterrupt() {
#endif

  // Data sent from the panel and keypads/modules has latency after a clock change (observed up to 160us for
  // keypad data).  The following sets up a timer for each platform that will call dscDataInterrupt() in
  // 250us to read the data line.

  // AVR Timer1 calls dscDataInterrupt() via ISR(TIMER1_OVF_vect) when the Timer1 counter overflows
  #if defined(__AVR__)
  TCNT1=61535;            // Timer1 counter start value, overflows at 65535 in 250us
  TCCR1B |= (1 << CS10);  // Sets the prescaler to 1

  // esp8266 timer1 calls dscDataInterrupt() in 250us
  #elif defined(ESP8266)
  timer1_write(1250);

  // esp32 timer1 calls dscDataInterrupt() in 250us
  #elif defined(ESP32)
  armSampleTimer();
  portENTER_CRITICAL_ISR(&timer1Mux);
  #endif


  // Panel sends data while the clock is high
  if (digitalRead(dscClockPin) == HIGH) {
    if (virtualKeypad) driveData(false);  // Restores the data line after a virtual keypad write
    previousClockHighTime_ = static_cast<std::uint32_t>(micros());
  }

  // Keypads and modules send data while the clock is low
  else {
    clockHighTime = static_cast<std::uint32_t>(micros() - previousClockHighTime_);  // Tracks the clock high time to find the reset between commands

    // Saves data and resets counters after the clock cycle is complete (high for at least 1ms)
    if (clockHighTime > 1000) {
      keybusTime = millis();
      seenClock_ = true;

      // Skips incomplete and redundant data from status commands - these are sent constantly on the keybus at a high
      // rate, so they are always skipped.  Checking is required in the ISR to prevent flooding the buffer.
      if (isrPanelBitTotal < 8) skipData_ = true;
      else switch (isrPanelData[0]) {

        case 0x05:  // Status: partitions 1-4
          if (redundantPanelData(previousCmd05_, isrPanelData, isrPanelByteCount)) skipData_ = true;
          break;

        case 0x1B:  // Status: partitions 5-8
          if (redundantPanelData(previousCmd1B_, isrPanelData, isrPanelByteCount)) skipData_ = true;
          break;
      }

      // Stores new panel data in the panel buffer
      currentCmd = isrPanelData[0];
      if (panelBufferLength == dscBufferSize) bufferOverflow = true;
      else if (!skipData_ && panelBufferLength < dscBufferSize) {
        for (byte i = 0; i < dscReadSize; i++) panelBuffer[panelBufferLength][i] = isrPanelData[i];
        panelBufferBitCount[panelBufferLength] = isrPanelBitTotal;
        panelBufferByteCount[panelBufferLength] = isrPanelByteCount;
        panelBufferLength++;
      }

      if (processModuleData) {

        // Stores new keypad and module data - this data is not buffered
        if (moduleDataDetected) {
          moduleCmd = isrPanelData[0];
          moduleSubCmd = isrPanelData[2];
          moduleDataDetected = false;
          moduleDataCaptured = true;  // Sets a flag for handleModule()
          for (byte i = 0; i < dscReadSize; i++) moduleData[i] = isrModuleData[i];
          moduleBitCount = isrPanelBitTotal;
          moduleByteCount = isrPanelByteCount;
        }

        // Resets the keypad and module capture data
        for (byte i = 0; i < dscReadSize; i++) isrModuleData[i] = 0;
      }

      // Resets the panel capture data and counters
      for (byte i = 0; i < dscReadSize; i++) isrPanelData[i] = 0;
      isrPanelBitTotal = 0;
      isrPanelBitCount = 0;
      isrPanelByteCount = 0;
      skipData_ = false;
    }

    // Virtual keypad
    if (virtualKeypad) {

      static bool writeCmd = false;

      if (writePartition <= 4 && statusCmd == 0x05) writeCmd = true;
      else if (writePartition >= 5 && statusCmd == 0x1B) writeCmd = true;
      else writeCmd = false;

      // Writes a F/A/P alarm key and repeats the key on the next immediate command from the panel (0x1C verification)
      if ((writeAlarm && writeKeyPending) || writeRepeat_) {

        // Writes the first bit by shifting the alarm key data right 7 bits and checking bit 0
        if (isrPanelBitTotal == 0) {
          if (!((writeKey >> 7) & 0x01)) {
            driveData(true);
          }
          writeStart_ = true;  // Resolves a timing issue where some writes do not begin at the correct bit
        }

        // Writes the remaining alarm key data
        else if (writeStart_ && isrPanelBitTotal <= 7) {
          if (!((writeKey >> (7 - isrPanelBitTotal)) & 0x01)) driveData(true);

          // Resets counters when the write is complete
          if (isrPanelBitTotal == 7) {
            writeKeyPending = false;
            writeStart_ = false;
            writeAlarm = false;

            // Sets up a repeated write for alarm keys
            if (!writeRepeat_) writeRepeat_ = true;
            else writeRepeat_ = false;
          }
        }
      }

      // Writes a regular key unless waiting for a response to the '*' key or the panel is sending a query command
      else if (writeKeyPending && !starKeyWait[writePartition - 1] && isrPanelByteCount == writeByte && writeCmd) {

        // Writes the first bit by shifting the key data right 7 bits and checking bit 0
        if (isrPanelBitTotal == writeBit) {
          if (!((writeKey >> 7) & 0x01)) driveData(true);
          writeStart_ = true;  // Resolves a timing issue where some writes do not begin at the correct bit
        }

        // Writes the remaining key data
        else if (writeStart_ && isrPanelBitTotal > writeBit && isrPanelBitTotal <= writeBit + 7) {
          if (!((writeKey >> (7 - isrPanelBitCount)) & 0x01)) driveData(true);

          // Resets counters when the write is complete
          if (isrPanelBitTotal == writeBit + 7) {
            if (starKeyCheck) starKeyWait[writePartition - 1] = true;  // Handles waiting until the panel is ready after pressing '*'
            else writeKeyPending = false;
            writeStart_ = false;
          }
        }
      }
    }
  }
  #if defined(ESP32)
  portEXIT_CRITICAL_ISR(&timer1Mux);
  #endif
}


// Interrupt function called by AVR Timer1, esp8266 timer1, and esp32 timer1 after 250us to read the data line
#if defined(__AVR__)
void Keybus::dscDataInterrupt() {
#elif defined(ESP8266)
void ICACHE_RAM_ATTR Keybus::dscDataInterrupt() {
#elif defined(ESP32)
void IRAM_ATTR Keybus::dscDataInterrupt() {
  stopSampleTimer();
  portENTER_CRITICAL_ISR(&timer1Mux);
#endif

  // Panel sends data while the clock is high
  if (digitalRead(dscClockPin) == HIGH) {

    // Reads panel data and sets data counters
    if (isrPanelByteCount < dscReadSize) {  // Limits Keybus data bytes to dscReadSize
      if (isrPanelBitCount < 8) {
        // Data is captured in each byte by shifting left by 1 bit and writing to bit 0
        isrPanelData[isrPanelByteCount] <<= 1;
        if (digitalRead(dscReadPin) == HIGH) {
          isrPanelData[isrPanelByteCount] |= 1;
        }
      }

      // Tests for a status command, used in dscClockInterrupt() to ensure keys are only written during a status command
      if (isrPanelBitTotal == 7) {
        switch (isrPanelData[0]) {
          case 0x05:
          case 0x0A: statusCmd = 0x05; break;
          case 0x1B: statusCmd = 0x1B; break;
          default: statusCmd = 0; break;
        }
      }

      // Stores the stop bit by itself in byte 1 - this aligns the Keybus bytes with panelData[] bytes
      if (isrPanelBitTotal == 8) {
        isrPanelBitCount = 0;
        isrPanelByteCount++;
      }

      // Increments the bit counter if the byte is incomplete
      else if (isrPanelBitCount < 7) {
        isrPanelBitCount++;
      }

      // Byte is complete, set the counters for the next byte
      else {
        isrPanelBitCount = 0;
        isrPanelByteCount++;
      }

      isrPanelBitTotal++;
    }
  }

  // Keypads and modules send data while the clock is low
  else {

    // Keypad and module data is not buffered and skipped if the panel data buffer is filling
    if (processModuleData && isrPanelByteCount < dscReadSize && panelBufferLength <= 1) {

      // Data is captured in each byte by shifting left by 1 bit and writing to bit 0
      if (isrPanelBitCount < 8) {
        isrModuleData[isrPanelByteCount] <<= 1;
        if (digitalRead(dscReadPin) == HIGH) {
          isrModuleData[isrPanelByteCount] |= 1;
        }
        else {
          moduleDataDetected = true;  // Keypads and modules send data by pulling the data line low
        }
      }

      // Stores the stop bit by itself in byte 1 - this aligns the Keybus bytes with moduleData[] bytes
      if (isrPanelBitTotal == 8) {
        isrModuleData[1] = 1;  // Sets the stop bit manually to 1 in byte 1
      }
    }
  }
  #if defined(ESP32)
  portEXIT_CRITICAL_ISR(&timer1Mux);
  #endif
}

} // namespace dsc
