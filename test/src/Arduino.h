// Minimal Arduino mock for native unit tests
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <algorithm>

typedef uint8_t byte;

inline long constrain(long val, long lo, long hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Stub millis/micros
static unsigned long _mock_millis = 0;
inline unsigned long millis() { return _mock_millis; }
inline void mock_set_millis(unsigned long ms) { _mock_millis = ms; }
inline void mock_advance_millis(unsigned long ms) { _mock_millis += ms; }

// Stub digitalRead/Write, analogRead, pinMode
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0
#define LED_BUILTIN 2

static int _mock_digital_pins[64] = {};
static int _mock_analog_pins[16] = {};

inline void pinMode(int pin, int mode) { (void)pin; (void)mode; }
inline void digitalWrite(int pin, int val) { if (pin >= 0 && pin < 64) _mock_digital_pins[pin] = val; }
inline int  digitalRead(int pin) { return (pin >= 0 && pin < 64) ? _mock_digital_pins[pin] : 0; }
inline int  analogRead(int pin) { return (pin >= 0 && pin < 16) ? _mock_analog_pins[pin] : 0; }
inline void mock_set_analog(int pin, int val) { if (pin >= 0 && pin < 16) _mock_analog_pins[pin] = val; }
inline void analogReadResolution(int bits) { (void)bits; }

enum { ADC_11db = 3 };
inline void analogSetAttenuation(int) {}

// String class stub (minimal)
#include <string>
class String {
public:
    std::string s;
    String() {}
    String(const char* c) : s(c ? c : "") {}
    String(int v) : s(std::to_string(v)) {}
    String(unsigned int v) : s(std::to_string(v)) {}
    String(long v) : s(std::to_string(v)) {}
    String(unsigned long v) : s(std::to_string(v)) {}
    String(float v, int dec = 2) { char b[32]; snprintf(b, sizeof(b), "%.*f", dec, v); s = b; }
    const char* c_str() const { return s.c_str(); }
    size_t length() const { return s.length(); }
    String operator+(const String& o) const { return String((s + o.s).c_str()); }
    String& operator+=(const String& o) { s += o.s; return *this; }
    bool operator==(const char* o) const { return s == o; }
    bool startsWith(const char* prefix) const { return s.rfind(prefix, 0) == 0; }
    bool equalsIgnoreCase(const char* o) const {
        if (s.size() != strlen(o)) return false;
        for (size_t i = 0; i < s.size(); i++)
            if (tolower(s[i]) != tolower(o[i])) return false;
        return true;
    }
    int toInt() const { return atoi(s.c_str()); }
    void toCharArray(char* buf, unsigned int len) const { strncpy(buf, s.c_str(), len); buf[len-1] = '\0'; }
};

inline String operator+(const char* lhs, const String& rhs) { return String((std::string(lhs) + rhs.s).c_str()); }

// Serial stub
struct MockSerial {
    void begin(int) {}
    void print(const char*) {}
    void println(const char*) {}
    void print(int) {}
    void println(int) {}
    void printf(const char* fmt, ...) {}
};
static MockSerial Serial;
