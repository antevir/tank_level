#pragma once

// ── ESP32-S2 Mini (Lolin S2 Mini) ──
#define DIST_UART_RX_PIN 33   // GPIO18 – A02YYUW TX → MCU RX
#define DIST_UART_TX_PIN 35   // GPIO33 – MCU TX → A02YYUW RX
#define CURRENT_ADC_PIN   3   // GPIO3  – Current sensor (ADC)
#define PUMP_RELAY_PIN    5   // GPIO5  – Pump relay
#define BUTTON_PIN       16   // GPIO16 – Button
#define SDCARD_CS_PIN    12   // GPIO12 – SD card CS
#define GOT_WATER_PIN    37   // GPIO37 – Water presence sensor
#define ONE_WIRE_PIN     18   // GPIO35 – DS18B20 air temperature

// SPI for SD card
#define SD_SCK_PIN        7   // GPIO7
#define SD_MISO_PIN       9   // GPIO9
#define SD_MOSI_PIN      11   // GPIO11

#define SD_APPEND FILE_APPEND
