#include "common.h"
#include "pins.h"

#include <esp_system.h>
#include <esp_task_wdt.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include "MedianFilterLib.h"

#include "tank.h"
#include "server.h"
#include "pump.h"

static WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_CLOCK_OFFSET, 60000);

static const char *g_reset_reason = "unknown";

static const char *resetReasonStr()
{
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "PowerOn";
    case ESP_RST_EXT:       return "External pin";
    case ESP_RST_SW:        return "Software";
    case ESP_RST_PANIC:     return "Panic/Exception";
    case ESP_RST_INT_WDT:   return "Interrupt WDT";
    case ESP_RST_TASK_WDT:  return "Task WDT";
    case ESP_RST_WDT:       return "Other WDT";
    case ESP_RST_DEEPSLEEP: return "Deep sleep";
    case ESP_RST_BROWNOUT:  return "Brownout";
    default:                return "Unknown";
  }
}

static void handleNtp()
{
  static long last_time = 0;

  if (millis() - last_time < 10000)
  {
    return;
  }
  last_time = millis();

  if (year() < 2000)
  {
    if (timeClient.update())
    {
      setTime(timeClient.getEpochTime());
      Log.info("NTP sync: %s", timeClient.getFormattedTime().c_str());
    }
    else
    {
      Log.warn("NTP sync failed");
    }
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  pinMode(SDCARD_CS_PIN, OUTPUT);
  pinMode(GOT_WATER_PIN, INPUT_PULLUP);

  digitalWrite(PUMP_RELAY_PIN, HIGH);
  digitalWrite(SDCARD_CS_PIN, HIGH);

  analogReadResolution(10);
  analogSetAttenuation(ADC_0db);

  // Remap SPI to custom pins; pass -1 for CS so SPI doesn't
  // reassign the CS pin — we drive it manually via SDCARD_CS_PIN.
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);

  // Connect to WiFi network first — Log uses UDP syslog so WiFi must be up.
  setupWifi();

  // Block until WiFi associates (max 15 s) so the very first log packets are delivered.
  {
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000)
      delay(100);
    delay(200); // Let DHCP/ARP settle before the first UDP packet
  }
  Log.begin();

  // Capture reset reason now (before any further init that could change it).
  g_reset_reason = resetReasonStr();
  Log.info("Reset reason: %s, heap: %d", g_reset_reason, ESP.getFreeHeap());

  Log.info("Setup 2: SD card");
  if (SD.begin(SDCARD_CS_PIN))
  {
    Log.info("SD card initialized");
    tank_set_sd_ok(true);
  }
  else
  {
    Log.error("SD card initialization failed!");
    tank_set_sd_ok(false);
  }

  Log.info("Setup 3: mDNS");
  if (!MDNS.begin(APP_NAME))
  {
    Log.error("Error setting up MDNS responder!");
  }
  MDNS.addService("http", "tcp", 80);

  Log.info("Free heap: %d", ESP.getFreeHeap());

  setupOta();

  timeClient.begin();

  tank_init();
  pump_init();
  server_init();

  Log.info("Setup complete, heap: %d", ESP.getFreeHeap());
}

void loop()
{
  esp_task_wdt_reset();

  ArduinoOTA.handle();
  handleNtp();
  tank_handle();
  server_handle();
  pump_handle();
  delay(10); // Yield to RTOS
}
