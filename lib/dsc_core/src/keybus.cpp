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
byte Keybus::dscClockPin;
byte Keybus::dscReadPin;
byte Keybus::dscWritePin;
char Keybus::writeKey;
byte Keybus::writePartition;
byte Keybus::writeByte;
byte Keybus::writeBit;
bool Keybus::virtualKeypad;
bool Keybus::processModuleData;
byte Keybus::panelData[dscReadSize];
byte Keybus::panelByteCount;
byte Keybus::panelBitCount;
volatile bool Keybus::writeKeyPending;
volatile byte Keybus::moduleData[dscReadSize];
volatile bool Keybus::moduleDataCaptured;
volatile bool Keybus::moduleDataDetected;
volatile byte Keybus::moduleByteCount;
volatile byte Keybus::moduleBitCount;
volatile bool Keybus::writeAlarm;
volatile bool Keybus::starKeyCheck;
volatile bool Keybus::starKeyWait[dscPartitions];
volatile bool Keybus::bufferOverflow;
volatile byte Keybus::panelBufferLength;
volatile byte Keybus::panelBuffer[dscBufferSize][dscReadSize];
volatile byte Keybus::panelBufferBitCount[dscBufferSize];
volatile byte Keybus::panelBufferByteCount[dscBufferSize];
volatile byte Keybus::isrPanelData[dscReadSize];
volatile byte Keybus::isrPanelByteCount;
volatile byte Keybus::isrPanelBitCount;
volatile byte Keybus::isrPanelBitTotal;
volatile byte Keybus::isrModuleData[dscReadSize];
volatile byte Keybus::currentCmd;
volatile byte Keybus::statusCmd;
volatile byte Keybus::moduleCmd;
volatile byte Keybus::moduleSubCmd;
volatile unsigned long Keybus::clockHighTime;
volatile unsigned long Keybus::keybusTime;

#if defined(ESP32)
portMUX_TYPE Keybus::timer1Mux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t * Keybus::timer1 = NULL;
#endif  // ESP32


Keybus *Keybus::owner_ = nullptr;
bool Keybus::seenClock_ = false;
bool Keybus::sharedDataPin_ = false;
std::uint32_t Keybus::previousClockHighTime_ = 0;
bool Keybus::skipData_ = false;
bool Keybus::writeStart_ = false;
bool Keybus::writeRepeat_ = false;
byte Keybus::previousCmd05_[dscReadSize]{};
byte Keybus::previousCmd1B_[dscReadSize]{};

Keybus::Keybus(byte clock, byte read, byte write)
    : configuredClock_(clock), configuredRead_(read), configuredWrite_(write) {
  processRedundantData = true;
}

bool Keybus::configurePins(byte clock, byte read, byte write) {
  if (started_ || clock == read || clock == write) return false;
  configuredClock_ = clock; configuredRead_ = read; configuredWrite_ = write;
  return true;
}

bool Keybus::begin() { return beginInternal(false); }

void Keybus::invalidateObserved() {
  knownPartitions_ = knownZoneGroups_ = 0;
  panelClock_.valid = eventClock_.valid = false;
  troubleKnown_ = false;
  for (byte i=0;i<dscPartitions;++i) {
    ready[i]=disabled[i]=armed[i]=armedStay[i]=armedAway[i]=false;
    noEntryDelay[i]=alarm[i]=fire[i]=entryDelay[i]=exitDelay[i]=false;
    exitState[i]=status[i]=lights[i]=0;
    fireKnown_[i]=false;
  }
}

bool Keybus::beginInternal(bool replay) {
  if (started_) return true;
  if (owner_ != nullptr) return false;
  if (!replay && (configuredClock_ == configuredRead_ || configuredClock_ == configuredWrite_)) return false;
  owner_ = this;
  replay_ = replay;
  invalidateObserved();
  ++stateVersion_;
  dscClockPin = configuredClock_;
  dscReadPin = configuredRead_;
  dscWritePin = configuredWrite_;
  virtualKeypad = replay_ || dscWritePin != 255;
  sharedDataPin_ = virtualKeypad && dscWritePin == dscReadPin;
  writePartition = 1;
  if (!replay_) {
    pinMode(dscClockPin, INPUT);
    pinMode(dscReadPin, INPUT);
    if (virtualKeypad) {
      // On a shared data GPIO, HIGH releases the open-drain line and LOW
      // pulls it down. A separate transistor driver uses opposite polarity.
      driveData(false);
      pinMode(dscWritePin, sharedDataPin_ ? OUTPUT_OPEN_DRAIN : OUTPUT);
    }
    setupTimer();
    if (timer1 == nullptr) { owner_ = nullptr; return false; }
  }
  seenClock_ = false;
  startupCycle_ = true;
  panelBufferIndex_ = 1;
  for (byte i = 0; i < dscReadSize; ++i) {
    previousCmd05_[i] = previousCmd1B_[i] = 0;
    previousCmd0A_[i] = previousCmd0F_[i] = 0;
    previousCmdE6_20_[i] = previousCmdE6_21_[i] = previousCmdE6_03_[i] = 0;
  }
  started_ = true;
  if (!replay_) attachInterrupt(digitalPinToInterrupt(dscClockPin), dscClockInterrupt, CHANGE);
  return true;
}


void Keybus::stop() {
  if (!started_ || owner_ != this) return;
  if (!replay_) detachInterrupt(digitalPinToInterrupt(dscClockPin));
  cancelTx();
  started_ = false;
  keybusConnected = false;
  knownPartitions_ = knownZoneGroups_ = 0;
  ++stateVersion_;
  owner_ = nullptr;

  if (!replay_) stopTimer();

  // Resets the panel capture data and counters
  panelBufferLength = 0;
  for (byte i = 0; i < dscReadSize; i++) isrPanelData[i] = 0;
  isrPanelBitTotal = 0;
  isrPanelBitCount = 0;
  isrPanelByteCount = 0;

  // Resets the keypad and module capture data
  for (byte i = 0; i < dscReadSize; i++) isrModuleData[i] = 0;
}


bool Keybus::loop() {

  // Wire completion is independent of whether the next periodic frame survives
  // duplicate suppression. Refresh admission before every possible early return.
  if (!started_) return false;
  writeReady = txCount_ == 0;

  #if defined(ESP8266) || defined(ESP32)
  yield();
  #endif

  // Checks if Keybus data is detected and sets a status flag if data is not detected for 3s
  #if defined(ESP32)
  portENTER_CRITICAL(&timer1Mux);
  #else
  noInterrupts();
  #endif

  if (!seenClock_ || static_cast<std::uint32_t>(millis() - keybusTime) > 3000) keybusConnected = false;  // keybusTime is set in dscDataInterrupt() when the clock resets
  else keybusConnected = true;

  #if defined(ESP32)
  portEXIT_CRITICAL(&timer1Mux);
  #else
  interrupts();
  #endif

  if (!keybusConnected && txCount_) cancelTx(TxOutcome::Disconnected);
  serviceTx();

  if (previousKeybus != keybusConnected) {
    previousKeybus = keybusConnected;
    if (!keybusConnected) {
      invalidateObserved();
      // A restarted panel may repeat the same status as before the outage.
      // Forget the ISR's duplicate cache so the first fresh status is decoded,
      // and discard any buffered frames that predate the disconnection.
      portENTER_CRITICAL(&timer1Mux);
      panelBufferLength = 0;
      for (byte i = 0; i < dscReadSize; ++i) previousCmd05_[i] = previousCmd1B_[i] = 0;
      portEXIT_CRITICAL(&timer1Mux);
      panelBufferIndex_ = 1;
      startupCycle_ = true;
      keybusVersion1 = false;
    }
    ++stateVersion_;
    keybusChanged = true;
    if (!pauseStatus) statusChanged = true;
    if (!keybusConnected) return true;
  }

  // Skips processing if the panel data buffer is empty
  if (panelBufferLength == 0) return false;

  // Copies data from the buffer to panelData[]
  byte dataIndex = panelBufferIndex_ - 1;
  for (byte i = 0; i < dscReadSize; i++) panelData[i] = panelBuffer[dataIndex][i];
  panelBitCount = panelBufferBitCount[dataIndex];
  panelByteCount = panelBufferByteCount[dataIndex];
  panelBufferIndex_++;

  // Resets counters when the buffer is cleared
  #if defined(ESP32)
  portENTER_CRITICAL(&timer1Mux);
  #else
  noInterrupts();
  #endif

  if (panelBufferIndex_ > panelBufferLength) {
    panelBufferIndex_ = 1;
    panelBufferLength = 0;
  }

  #if defined(ESP32)
  portEXIT_CRITICAL(&timer1Mux);
  #else
  interrupts();
  #endif

  if (!validFrame()) return false;
  if (panelData[0] != 0x05 && panelData[0] != 0x1B && !validChecksum()) return false;

  // Waits at startup for the 0x05 status command or a command with valid CRC data to eliminate spurious data.
  if (startupCycle_) {
    if (panelData[0] == 0) return false;
    else if (panelData[0] == 0x05 || panelData[0] == 0x1B) {
      if (panelByteCount == 6) keybusVersion1 = true;
      startupCycle_ = false;
      writeReady = true;
    }
    else if (!validChecksum()) return false;
  }

  // Sets writeReady status
  writeReady = txCount_ == 0;

  // Skips redundant data sent constantly while in installer programming
  switch (panelData[0]) {
    case 0x0A:  // Partition 1 status in programming
      if (redundantPanelData(previousCmd0A_, panelData)) return false;
      break;

    case 0x0F:  // Partition 2 status in programming
      if (redundantPanelData(previousCmd0F_, panelData)) return false;
      break;

    case 0xE6:
      if (panelData[2] == 0x20 && redundantPanelData(previousCmdE6_20_, panelData)) return false;  // Partition 1 status in programming, zone lights 33-64
      if (panelData[2] == 0x21 && redundantPanelData(previousCmdE6_21_, panelData)) return false;  // Partition 2 status in programming
      break;
  }
  if (dscPartitions > 4) {
    if (panelData[0] == 0xE6 && panelData[2] == 0x03 && redundantPanelData(previousCmdE6_03_, panelData, 8)) return false;  // Status in alarm/programming, partitions 5-8
  }

  ++stateVersion_;

  // Processes valid panel data
  switch (panelData[0]) {
    case 0x05:                                                     // Panel status: partitions 1-4
    case 0x1B: processPanelStatus(); break;                        // Panel status: partitions 5-8
    case 0x16: processPanel_0x16(); break;                         // Panel configuration
    case 0x27: processPanel_0x27(); break;                         // Panel status and zones 1-8 status
    case 0x2D: processPanel_0x2D(); break;                         // Panel status and zones 9-16 status
    case 0x34: processPanel_0x34(); break;                         // Panel status and zones 17-24 status
    case 0x3E: processPanel_0x3E(); break;                         // Panel status and zones 25-32 status
    case 0x87: processPanel_0x87(); break;                         // PGM outputs
    case 0xA5: processPanel_0xA5(); break;                         // Date, time, system status messages - partitions 1-2
    case 0xE6: if (dscPartitions > 2) processPanel_0xE6(); break;  // Extended status command split into multiple subcommands to handle up to 8 partitions/64 zones
    case 0xEB: if (dscPartitions > 2) processPanel_0xEB(); break;  // Date, time, system status messages - partitions 1-8
  }

  return true;
}


bool Keybus::handleModule() {
  if (!moduleDataCaptured) return false;
  moduleDataCaptured = false;

  if (moduleBitCount < 8) return false;

  // Determines if a keybus message is a response to a panel command
  switch (moduleCmd) {
    case 0x11:
    case 0x28:
    case 0xD5: queryResponse = true; break;
    default: queryResponse = false; break;
  }

  return true;
}

bool Keybus::validChecksum() {
  if (panelByteCount < 3 || panelByteCount > dscReadSize || panelBitCount < 9) return false;
  byte byteCount = (panelBitCount - 1) / 8;
  if (byteCount >= panelByteCount || byteCount >= dscReadSize) return false;
  int dataSum = 0;
  for (byte panelByte = 0; panelByte < byteCount; panelByte++) {
    if (panelByte != 1) dataSum += panelData[panelByte];
  }
  if (dataSum % 256 == panelData[byteCount]) return true;
  else return false;
}



} // namespace dsc
