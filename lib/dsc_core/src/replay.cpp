// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only transport boundary. The normal decoder/transmit queue are unchanged.
#include "keybus.h"
#ifdef DSC_ENABLE_REPLAY
namespace dsc {
bool Keybus::beginReplay() { return beginInternal(true); }

bool Keybus::injectReplayFrame(const Frame &frame) {
  if (!started_ || !replay_ || frame.size < 2 || frame.size > FRAME_CAPACITY) return false;
  if (panelBufferLength >= dscBufferSize) { bufferOverflow = true; return false; }
  const auto slot = panelBufferLength;
  for (byte i=0; i<dscReadSize; ++i) panelBuffer[slot][i] = frame.bytes[i];
  panelBufferByteCount[slot] = frame.size;
  panelBufferBitCount[slot] = (frame.size - 2) * 8 + 9;
  ++panelBufferLength;
  seenClock_ = true;
  keybusTime = millis();
  return true;
}

bool Keybus::takeReplayKey(char &key, byte &partition) {
  if (!started_ || !replay_ || !txActive_) return false;
  if (!writeKeyPending && writeRepeat_) {
    key = txQueue_[txHead_].key;
    partition = writePartition;
    writeRepeat_ = false;
    return true;
  }
  if (!writeKeyPending || starKeyWait[writePartition-1]) return false;
  key = txQueue_[txHead_].key;
  partition = writePartition;
  if (writeAlarm) { writeKeyPending = false; writeAlarm = false; writeRepeat_ = true; }
  else if (key == '*' && starKeyCheck) starKeyWait[partition-1] = true;
  else writeKeyPending = false;
  return true;
}
}  // namespace dsc
#endif
