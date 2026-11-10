// Separate process for develop's diagnostic Reader. It must not run a second
// independent capture instance alongside the production interface on one bus.
#define dscKeybusReader
#include "dscKeybusInterface.h"
#include "fake_arduino.h"
#include "powerseries.h"
#include <iostream>

static dscKeybusReaderInterface reader(22, 21);

int main() {
  reader.begin();
  fake::frame(fixtures::status());
  reader.loop();
  Serial.output.clear();
  reader.printPanelCommand();
  if (Serial.output != "0x05") return 1;
  Serial.output.clear();
  reader.printPanelMessage();
  if (Serial.output.find("Ready") == std::string::npos) return 2;
  std::cout << "PASS develop Reader diagnostics (separate capture process)\n";
  return 0;
}
