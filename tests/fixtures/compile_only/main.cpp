#include <Arduino.h>
#include <dscKeybusInterface.h>

// Compilation fixture only. Write pin omitted: virtual keypad is disabled.
// There is no network stack, configuration for a real alarm, or upload script.
dscKeybusInterface dsc(22, 21);

void setup() { dsc.begin(); }
void loop() { dsc.loop(); }

