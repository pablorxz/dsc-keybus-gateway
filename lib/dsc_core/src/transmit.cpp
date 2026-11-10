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
#include <cstring>

namespace dsc {
// A single loop owner admits complete sequences. ISR only consumes the current
// encoded key; no application/network code is invoked from an interrupt.
bool Keybus::validKey(char key) {
  const char *allowed = "0123456789*#fFbB><lLsSwWnNaAcCrRpPxX[]{}";
  return key != 0 && std::strchr(allowed, key) != nullptr;
}

TxAdmission Keybus::write(char key) {
  const char keys[] = {key, 0};
  return enqueue(keys, writePartition);
}
TxAdmission Keybus::write(const char *keys) { return enqueue(keys, writePartition); }

TxAdmission Keybus::enqueue(const char *keys, byte partition) {
  if (partition == 0 || partition > dscPartitions) return TxAdmission::InvalidPartition;
  if (!started_ || !keybusConnected || disabled[partition-1] || status[partition-1] == 0)
    return TxAdmission::Unavailable;
  if (!virtualKeypad) return TxAdmission::ReadOnly;
  if (keys == nullptr || *keys == 0) return TxAdmission::InvalidKey;
  byte count = 0;
  while (count < TX_CAPACITY && keys[count] != 0) {
    if (!validKey(keys[count])) return TxAdmission::InvalidKey;
    ++count;
  }
  if (count == TX_CAPACITY && keys[count] != 0) return TxAdmission::TooLong;
  if (count > TX_CAPACITY - txCount_) return TxAdmission::QueueFull;
  for (byte i = 0; i < count; ++i) {
    txQueue_[(txHead_ + txCount_) % TX_CAPACITY] = {keys[i], partition};
    ++txCount_;
  }
  txOutcome_ = TxOutcome::Pending;
  writeReady = false;
  serviceTx();
  return TxAdmission::Accepted;
}

void Keybus::cancelTx(TxOutcome reason) {
  if (owner_ != this) return;
  portENTER_CRITICAL(&timer1Mux);
  writeKeyPending = false;
  writeStart_ = writeRepeat_ = false;
  writeAlarm = false;
  starKeyCheck = false;
  for (byte p = 0; p < dscPartitions; ++p) starKeyWait[p] = false;
  if (virtualKeypad && !replay_) driveData(false);
  portEXIT_CRITICAL(&timer1Mux);
  for (auto &item : txQueue_) item = {};
  txCount_ = txHead_ = 0;
  txActive_ = false;
  txOutcome_ = reason;
  writeReady = true;
}

void Keybus::serviceTx() {
  const auto now = static_cast<std::uint32_t>(millis());
  if (txActive_ && static_cast<std::uint32_t>(now - txStarted_) >= TX_KEY_TIMEOUT_MS) {
    cancelTx(TxOutcome::TimedOut);
    return;
  }
  // Fire/aux/panic keys have a mandatory second transmission. The ISR clears
  // writeKeyPending after the first byte but keeps writeRepeat_ set until the
  // verification byte is fully sent; do not advance the FIFO in between.
  portENTER_CRITICAL(&timer1Mux);
  const bool keyComplete = !writeKeyPending && !writeRepeat_;
  portEXIT_CRITICAL(&timer1Mux);
  if (txActive_ && keyComplete) {
    txQueue_[txHead_] = {};
    txHead_ = (txHead_ + 1) % TX_CAPACITY;
    --txCount_;
    txActive_ = false;
    if (txCount_ == 0) txOutcome_ = TxOutcome::Transmitted;
  }
  if (!txActive_ && txCount_ != 0) {
    const auto item = txQueue_[txHead_];
    portENTER_CRITICAL(&timer1Mux);
    writePartition = item.partition;
    setWriteKey(item.key);
    txActive_ = writeKeyPending;
    portEXIT_CRITICAL(&timer1Mux);
    if (txActive_) txStarted_ = now;
  }
  writeReady = txCount_ == 0;
}


// Specifies the key value to be written by dscClockInterrupt() and selects the write partition.  This includes a 500ms
// delay after alarm keys to resolve errors when additional keys are sent immediately after alarm keys.
void Keybus::setWriteKey(const char receivedKey) {
  static unsigned long previousTime;
  static bool setPartition;

  // Sets the write partition if set by virtual keypad key '/'
  if (setPartition) {
    setPartition = false;
    if (receivedKey >= '1' && receivedKey <= '8') {
      writePartition = receivedKey - 48;
    }
    return;
  }

  // Sets the binary to write for virtual keypad keys
  if (!writeKeyPending && (millis() - previousTime > 500 || millis() <= 500)) {
    bool validKey = true;

    // Skips writing to disabled partitions or partitions not specified in Keybus.h
    if (disabled[writePartition - 1] || dscPartitions < writePartition) {
      switch (receivedKey) {
        case '/': setPartition = true; validKey = false; break;
      }
      return;
    }

    // Sets binary for virtual keypad keys
    else {
      switch (receivedKey) {
        case '/': setPartition = true; validKey = false; break;
        case '0': writeKey = 0x00; break;
        case '1': writeKey = 0x05; break;
        case '2': writeKey = 0x0A; break;
        case '3': writeKey = 0x0F; break;
        case '4': writeKey = 0x11; break;
        case '5': writeKey = 0x16; break;
        case '6': writeKey = 0x1B; break;
        case '7': writeKey = 0x1C; break;
        case '8': writeKey = 0x22; break;
        case '9': writeKey = 0x27; break;
        case '*': writeKey = 0x28; if (status[writePartition - 1] < 0x9E) starKeyCheck = true; break;
        case '#': writeKey = 0x2D; break;
        case 'f': case 'F': writeKey = 0xBB; writeAlarm = true; break;                           // Keypad fire alarm
        case 'b': case 'B': writeKey = 0x82; break;                                              // Enter event buffer
        case '>': writeKey = 0x87; break;                                                        // Event buffer right arrow
        case '<': writeKey = 0x88; break;                                                        // Event buffer left arrow
        case 'l': case 'L': writeKey = 0xA5; break;                                              // LCD keypad data request
        case 's': case 'S': writeKey = 0xAF; writeAccessCode[writePartition - 1] = true; break;  // Arm stay
        case 'w': case 'W': writeKey = 0xB1; writeAccessCode[writePartition - 1] = true; break;  // Arm away
        case 'n': case 'N': writeKey = 0xB6; writeAccessCode[writePartition - 1] = true; break;  // Arm with no entry delay (night arm)
        case 'a': case 'A': writeKey = 0xDD; writeAlarm = true; break;                           // Keypad auxiliary alarm
        case 'c': case 'C': writeKey = 0xBB; break;                                              // Door chime
        case 'r': case 'R': writeKey = 0xDA; break;                                              // Reset
        case 'p': case 'P': writeKey = 0xEE; writeAlarm = true; break;                           // Keypad panic alarm
        case 'x': case 'X': writeKey = 0xE1; break;                                              // Exit
        case '[': writeKey = 0xD5; writeAccessCode[writePartition - 1] = true; break;            // Command output 1
        case ']': writeKey = 0xDA; writeAccessCode[writePartition - 1] = true; break;            // Command output 2
        case '{': writeKey = 0x70; writeAccessCode[writePartition - 1] = true; break;            // Command output 3
        case '}': writeKey = 0xEC; writeAccessCode[writePartition - 1] = true; break;            // Command output 4
        default: {
          validKey = false;
          break;
        }
      }
    }

    // Sets the writing position in dscClockInterrupt() for the currently set partition
    switch (writePartition) {
      case 1:
      case 5: {
        writeByte = 2;
        writeBit = 9;
        break;
      }
      case 2:
      case 6: {
        writeByte = 3;
        writeBit = 17;
        break;
      }
      case 3:
      case 7: {
        writeByte = 8;
        writeBit = 57;
        break;
      }
      case 4:
      case 8: {
        writeByte = 9;
        writeBit = 65;
        break;
      }
      default: {
        writeByte = 2;
        writeBit = 9;
        break;
      }
    }

    if (writeAlarm) previousTime = millis();  // Sets a marker to time writes after keypad alarm keys
    if (validKey) {
      writeKeyPending = true;                 // Sets a flag indicating that a write is pending, cleared by dscClockInterrupt()
      writeReady = false;
    }
  }
}



} // namespace dsc
