#pragma once

#ifdef ESP32
// ── ESP32-S2 Mini (Lolin S2 Mini) ──
// Pin-compatible board position mapping with D1 Mini.
//   Position  D1 Mini  ESP8266 GPIO  ESP32-S2 GPIO
//   D0        D0       GPIO16        GPIO5
//   A0        A0       ADC0          GPIO3
//   D1        D1       GPIO5         GPIO35
//   D2        D2       GPIO4         GPIO33
//   D3        D3       GPIO0         GPIO18
//   D4        D4       GPIO2         GPIO16
//   D5        D5       GPIO14        GPIO7
//   D6        D6       GPIO12        GPIO9
//   D7        D7       GPIO13        GPIO11
//   D8        D8       GPIO15        GPIO12

#define DIST_TRIG_PIN    18   // D3 position
#define DIST_ECHO_PIN    33   // D2 position
#define CURRENT_ADC_PIN   3   // A0 position
#define PUMP_RELAY_PIN    5   // D0 position
#define BUTTON_PIN       16   // D4 position
#define SDCARD_CS_PIN    12   // D8 position
#define GOT_WATER_PIN    35   // D1 position

// SPI for SD card (D5/D6/D7 positions)
#define SD_SCK_PIN        7   // D5 position
#define SD_MISO_PIN       9   // D6 position
#define SD_MOSI_PIN      11   // D7 position

// ESP32 SD library: FILE_WRITE truncates, FILE_APPEND appends
#define SD_APPEND FILE_APPEND

#else
// ── ESP8266 D1 Mini ──
#define D0 16
#define D1 5
#define D2 4
#define D3 0
#define D4 2
#define D5 14
#define D6 12
#define D7 13
#define D8 15

#define DIST_TRIG_PIN D3
#define DIST_ECHO_PIN D2
#define CURRENT_ADC_PIN A0
#define PUMP_RELAY_PIN D0
#define BUTTON_PIN D4
#define SDCARD_CS_PIN D8
#define GOT_WATER_PIN D1

// ESP8266 SD library: FILE_WRITE already appends
#define SD_APPEND FILE_WRITE

#endif
