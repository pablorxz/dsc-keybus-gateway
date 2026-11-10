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

#ifndef DSC_CORE_KEYBUS_H
#define DSC_CORE_KEYBUS_H

#include <Arduino.h>
#include <array>
#include "transmit.h"
#include "frame.h"
#include "state.h"

namespace dsc {

#if defined(__AVR__)
const byte dscPartitions = 4;   // Maximum number of partitions - requires 19 bytes of memory per partition
const byte dscZones = 4;        // Maximum number of zone groups, 8 zones per group - requires 6 bytes of memory per zone group
const byte dscBufferSize = 10;  // Number of commands to buffer if the sketch is busy - requires dscReadSize + 2 bytes of memory per command
const byte dscReadSize = 16;    // Maximum bytes of a Keybus command
#elif defined(ESP8266)
const byte dscPartitions = 8;
const byte dscZones = 8;
const byte dscBufferSize = 50;
const byte dscReadSize = 16;
#elif defined(ESP32)
const byte dscPartitions = 8;
const byte dscZones = 8;
const DRAM_ATTR byte dscBufferSize = 50;
const DRAM_ATTR byte dscReadSize = 16;
#endif

// Exit delay target states
#define DSC_EXIT_STAY 1
#define DSC_EXIT_AWAY 2
#define DSC_EXIT_NO_ENTRY_DELAY 3


class Keybus {

  public:

    // Initializes writes as disabled by default
    Keybus(byte setClockPin, byte setReadPin, byte setWritePin = 255);
    ~Keybus() { stop(); }
    Keybus(const Keybus &) = delete;
    Keybus &operator=(const Keybus &) = delete;

    // Configure only while stopped; 255 disables writing.
    bool configurePins(byte clock, byte read, byte write = 255);

    // Interface control
    bool begin();                                    // One physical bus owner; no network prerequisite
    bool loop();                                      // Returns true if valid panel data is available
    void stop();                                      // Disables the clock hardware interrupt and data timer interrupt
    void resetStatus();                               // Resets the state of all status components as changed for sketches to get the current status

    // Atomic nonblocking admission; input is validated and copied into owned storage.
    TxAdmission enqueue(const char *keys, byte partition);
    TxAdmission write(char key);
    TxAdmission write(const char *keys);
    void cancelTx(TxOutcome reason = TxOutcome::Canceled);
    byte pendingKeys() const { return txCount_; }
    TxOutcome txOutcome() const { return txOutcome_; }
    bool running() const { return started_; }
    PanelState snapshot() const;
#ifdef DSC_ENABLE_REPLAY
    bool beginReplay();
    bool injectReplayFrame(const Frame &frame);
    bool takeReplayKey(char &key, byte &partition);
#endif

    // Write control
    static byte writePartition;                       // Set to a partition number for virtual keypad
    bool writeReady{};                                  // True if the library is ready to write a key

    // Prints output to the stream interface set in begin()

    // These can be configured in the sketch setup() before begin()
    bool hideKeypadDigits{};          // Controls if keypad digits are hidden for publicly posted logs (default: false)
    static bool processModuleData;  // Controls if keypad and module data is processed and displayed (default: false)
    bool displayTrailingBits{};       // Controls if bits read as the clock is reset are displayed, appears to be spurious data (default: false)

    // Panel time
    bool timestampChanged{};          // True after the panel sends a timestamped message
    byte hour{}, minute{}, day{}, month{};
    int year{};

    // Sets panel time, the year can be sent as either 2 or 4 digits, returns true if the panel is ready to set the time

    // Status tracking
    bool statusChanged{};                   // True after any status change
    bool pauseStatus{};                     // Prevent status from showing as changed, set in sketch to control when to update status
    bool keybusConnected{}, keybusChanged{};  // True if data is detected on the Keybus
    byte accessCode[dscPartitions]{};
    bool accessCodeChanged[dscPartitions]{};
    bool accessCodePrompt{};                // True if the panel is requesting an access code
    bool decimalInput{};                    // True if the panel is requesting 3 digit input (for 0x6E readout)
    bool trouble{}, troubleChanged{};
    bool powerTrouble{}, powerChanged{};
    bool batteryTrouble{}, batteryChanged{};
    bool keypadFireAlarm{}, keypadAuxAlarm{}, keypadPanicAlarm{};
    bool ready[dscPartitions]{}, readyChanged[dscPartitions]{};
    bool disabled[dscPartitions]{}, disabledChanged[dscPartitions]{};
    bool armed[dscPartitions]{}, armedAway[dscPartitions]{}, armedStay[dscPartitions]{};
    bool noEntryDelay[dscPartitions]{}, armedChanged[dscPartitions]{};
    bool alarm[dscPartitions]{}, alarmChanged[dscPartitions]{};
    bool exitDelay[dscPartitions]{}, exitDelayChanged[dscPartitions]{};
    byte exitState[dscPartitions]{}, exitStateChanged[dscPartitions]{};
    bool entryDelay[dscPartitions]{}, entryDelayChanged[dscPartitions]{};
    bool fire[dscPartitions]{}, fireChanged[dscPartitions]{};
    bool openZonesStatusChanged{};
    byte openZones[dscZones]{}, openZonesChanged[dscZones]{};    // Zone status is stored in an array using 1 bit per zone, up to 64 zones
    bool alarmZonesStatusChanged{};
    byte alarmZones[dscZones]{}, alarmZonesChanged[dscZones]{};  // Zone alarm status is stored in an array using 1 bit per zone, up to 64 zones
    bool pgmOutputsStatusChanged{};
    byte pgmOutputs[2]{}, pgmOutputsChanged[2]{};
    byte panelVersion{};

    /* panelData[] and moduleData[] store panel and keypad/module data in an array: command [0], stop bit by itself [1],
     * followed by the remaining data.  These can be accessed directly in the sketch to get data that is not already
     * tracked in the library.  See dscKeybusPrintData.cpp for the currently known DSC commands and data.
     *
     * panelData[] example:
     *   Byte 0     Byte 2   Byte 3   Byte 4   Byte 5
     *   00000101 0 10000001 00000001 10010001 11000111 [0x05] Partition 1: Ready Backlight - Partition ready | Partition 2: disabled
     *            ^ Byte 1 (stop bit)
     */
    static byte panelData[dscReadSize];
    static volatile byte moduleData[dscReadSize];

    // status[] and lights[] store the current status message and LED state for each partition.  These can be accessed
    // directly in the sketch to get data that is not already tracked in the library.  See printPanelMessages() and
    // printPanelLights() in dscKeybusPrintData.cpp to see how this data translates to the status message and LED status.
    byte status[dscPartitions]{};
    byte lights[dscPartitions]{};

    // Process keypad and module data, returns true if data is available
    bool handleModule();

    // True if dscBufferSize needs to be increased
    static volatile bool bufferOverflow;

    // Timer interrupt function to capture data - declared as public for use by AVR Timer1
    static void dscDataInterrupt();

    // Deprecated
    bool processRedundantData{};  // Controls if repeated periodic commands are processed and displayed (default: false)

  private:

    static void setupTimer();
    static void stopTimer();
    static void IRAM_ATTR armSampleTimer();
    static void IRAM_ATTR stopSampleTimer();
    static void IRAM_ATTR driveData(bool sink);
    bool beginInternal(bool replay);
    void invalidateObserved();
    bool troubleKnown_{false}, fireKnown_[dscPartitions]{};
    bool validFrame() const;

    void processPanelStatus();
    void processPanelStatus0(byte partition, byte panelByte);
    void processPanelStatus1(byte partition, byte panelByte);
    void processPanelStatus2(byte partition, byte panelByte);
    void processPanelStatus4(byte partition, byte panelByte);
    void processPanelStatus5(byte partition, byte panelByte);
    void processPanel_0x16();
    void processPanel_0x27();
    void processPanel_0x2D();
    void processPanel_0x34();
    void processPanel_0x3E();
    void processPanel_0x87();
    void processPanel_0xA5();
    void processPanel_0xE6();
    void processPanel_0xE6_0x09();
    void processPanel_0xE6_0x0B();
    void processPanel_0xE6_0x0D();
    void processPanel_0xE6_0x0F();
    void processPanel_0xE6_0x1A();
    void processPanel_0xEB();
    void processReadyStatus(byte partitionIndex, bool status);
    void processAlarmStatus(byte partitionIndex, bool status);
    void processExitDelayStatus(byte partitionIndex, bool status);
    void processEntryDelayStatus(byte partitionIndex, bool status);
    void processNoEntryDelayStatus(byte partitionIndex, bool status);
    void processZoneStatus(byte zonesByte, byte panelByte);
    void processTime(byte panelByte);
    void processAlarmZones(byte panelByte, byte startByte, byte zoneCountOffset, byte writeValue);
    void processAlarmZonesStatus(byte zonesByte, byte zoneCount, byte writeValue);
    void processArmed(byte partitionIndex, bool armedStatus);
    void processPanelAccessCode(byte partitionIndex, byte dscCode, bool accessCodeIncrease = true);




    bool validChecksum();
    void setWriteKey(const char receivedKey);
    void serviceTx();
    static bool validKey(char key);
    static void dscClockInterrupt();
    static bool redundantPanelData(byte previousCmd[], volatile byte currentCmd[], byte checkedBytes = dscReadSize);

    #if defined(ESP32)
    static hw_timer_t * timer1;
    static portMUX_TYPE timer1Mux;
    #endif

    byte configuredClock_{22}, configuredRead_{21}, configuredWrite_{255};
    bool started_{false};
    bool replay_{false};
    std::uint32_t stateVersion_{0};
    byte knownPartitions_{0}, knownZoneGroups_{0};
    PanelClock panelClock_{}, eventClock_{};
    std::array<TxKey, TX_CAPACITY> txQueue_{};
    byte txHead_{0}, txCount_{0};
    bool txActive_{false};
    std::uint32_t txStarted_{0};
    TxOutcome txOutcome_{TxOutcome::Idle};
    static Keybus *owner_;
    bool startupCycle_{true};
    byte panelBufferIndex_{1};
    byte previousCmd0A_[dscReadSize]{}, previousCmd0F_[dscReadSize]{};
    byte previousCmdE6_20_[dscReadSize]{}, previousCmdE6_21_[dscReadSize]{};
    byte previousCmdE6_03_[dscReadSize]{};
    static bool seenClock_;
    static bool sharedDataPin_;
    static std::uint32_t previousClockHighTime_;
    static bool skipData_, writeStart_, writeRepeat_;
    static byte previousCmd05_[dscReadSize], previousCmd1B_[dscReadSize];
    bool writeAccessCode[dscPartitions]{};
    bool queryResponse{};
    bool previousTrouble{};
    bool previousKeybus{};
    bool previousPower{};
    bool previousDisabled[dscPartitions]{};
    byte previousAccessCode[dscPartitions]{};
    byte previousLights[dscPartitions]{}, previousStatus[dscPartitions]{};
    bool previousReady[dscPartitions]{};
    bool previousExitDelay[dscPartitions]{}, previousEntryDelay[dscPartitions]{};
    byte previousExitState[dscPartitions]{};
    bool previousArmed[dscPartitions]{}, previousArmedStay[dscPartitions]{}, previousNoEntryDelay[dscPartitions]{};
    bool previousAlarm[dscPartitions]{};
    bool previousFire[dscPartitions]{};
    byte previousOpenZones[dscZones]{}, previousAlarmZones[dscZones]{};
    byte previousPgmOutputs[2]{};
    bool keybusVersion1{};

    static byte dscClockPin;
    static byte dscReadPin;
    static byte dscWritePin;
    static byte writeByte, writeBit;
    static bool virtualKeypad;
    static char writeKey;
    static byte panelBitCount, panelByteCount;
    static volatile bool writeKeyPending;
    static volatile bool writeAlarm, starKeyCheck, starKeyWait[dscPartitions];
    static volatile bool moduleDataDetected, moduleDataCaptured;
    static volatile unsigned long clockHighTime, keybusTime;
    static volatile byte panelBufferLength;
    static volatile byte panelBuffer[dscBufferSize][dscReadSize];
    static volatile byte panelBufferBitCount[dscBufferSize], panelBufferByteCount[dscBufferSize];
    static volatile byte moduleBitCount, moduleByteCount;
    static volatile byte currentCmd, statusCmd, moduleCmd, moduleSubCmd;
    static volatile byte isrPanelData[dscReadSize], isrPanelBitTotal, isrPanelBitCount, isrPanelByteCount;
    static volatile byte isrModuleData[dscReadSize];
};

} // namespace dsc

#endif // DSC_CORE_KEYBUS_H
