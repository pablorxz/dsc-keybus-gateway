#pragma once

// Host-only Arduino surface. No physical GPIO, networking, serial ports or ESPs.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

using byte = std::uint8_t;
class __FlashStringHelper;
#define F(value) reinterpret_cast<const __FlashStringHelper *>(value)
#define IRAM_ATTR
#define DRAM_ATTR
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define OUTPUT_OPEN_DRAIN 3
#define CHANGE 2
#define HEX 16
#define DEC 10
#define BIN 2
#define bitRead(value, bit) (((value) >> (bit)) & 1U)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, state) ((state) ? bitSet(value, bit) : bitClear(value, bit))

class Stream {
 public:
  std::string output;
  std::size_t print(const char *value) { output += value; return std::strlen(value); }
  std::size_t print(const __FlashStringHelper *value) {
    return print(reinterpret_cast<const char *>(value));
  }
  std::size_t print(char value) { output += value; return 1; }
  template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
  std::size_t print(T value, int base = DEC) {
    const auto start = output.size();
    if constexpr (std::is_signed_v<T>) {
      if (value < 0) { output += '-'; value = -value; }
    }
    auto number = static_cast<unsigned long long>(value);
    char digits[65];
    std::size_t count = 0;
    do { digits[count++] = "0123456789ABCDEF"[number % base]; number /= base; } while (number);
    while (count) output += digits[--count];
    return output.size() - start;
  }
  void println() { output += '\n'; }
  template <typename T> void println(const T &value) { print(value); println(); }
};

extern Stream Serial;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL(portMUX_TYPE *) {}
#define portENTER_CRITICAL_ISR portENTER_CRITICAL
#define portEXIT_CRITICAL_ISR portEXIT_CRITICAL
struct hw_timer_t { void (*callback)() = nullptr; bool enabled = false; bool running = false; unsigned long interval = 250; };

unsigned long millis();
unsigned long micros();
void yield();
void pinMode(byte pin, int mode);
int digitalRead(byte pin);
void digitalWrite(byte pin, int value);
inline int digitalPinToInterrupt(byte pin) { return pin; }
void attachInterrupt(int pin, void (*callback)(), int mode);
void detachInterrupt(int pin);
hw_timer_t *timerBegin(int timer, int divider, bool count_up);
void timerStop(hw_timer_t *timer);
void timerStart(hw_timer_t *timer);
void timerWrite(hw_timer_t *timer, std::uint64_t value);
void timerAttachInterrupt(hw_timer_t *timer, void (*callback)(), bool edge);
void timerAlarmWrite(hw_timer_t *timer, unsigned long interval, bool repeat);
void timerAlarmEnable(hw_timer_t *timer);
void timerAlarmDisable(hw_timer_t *timer);
void timerEnd(hw_timer_t *timer);
char *itoa(int value, char *buffer, int base);

