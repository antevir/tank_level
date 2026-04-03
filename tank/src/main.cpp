#include "common.h"
#include "pins.h"

#ifdef ESP32
#include <esp_system.h>
#include <esp_task_wdt.h>
#endif

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

#ifdef ESP32
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
#endif

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
  pinMode(BUTTON_PIN, INPUT_PULLUP);   // Active-low button; pullup prevents floating
  pinMode(DIST_ECHO_PIN, INPUT);       // Will be overridden below per platform
  pinMode(DIST_TRIG_PIN, OUTPUT);
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  pinMode(SDCARD_CS_PIN, OUTPUT);
  pinMode(GOT_WATER_PIN, INPUT_PULLUP);

  digitalWrite(DIST_TRIG_PIN, LOW);
  digitalWrite(PUMP_RELAY_PIN, HIGH);
  digitalWrite(SDCARD_CS_PIN, HIGH);

#ifdef ESP32
  // ESP32-S2 Mini: ADC defaults to 13 bits, match ESP8266 10-bit range
  analogReadResolution(10);
  analogSetPinAttenuation(CURRENT_ADC_PIN, ADC_11db);

  // Remap SPI to D5/D6/D7 board positions; pass -1 for CS so SPI doesn't
  // reassign the CS pin — we drive it manually via SDCARD_CS_PIN.
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);

  // Pull echo pin LOW — HC-SR04 echo is idle-LOW, prevents floating panic.
  pinMode(DIST_ECHO_PIN, INPUT_PULLDOWN);
#else
  // ESP8266: no hardware pulldown; pullup prevents floating.
  pinMode(DIST_ECHO_PIN, INPUT_PULLUP);
#endif

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
#ifdef ESP32
  g_reset_reason = resetReasonStr();
#else
  static String s_reset_reason_str = ESP.getResetReason();
  g_reset_reason = s_reset_reason_str.c_str();
#endif
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

#ifdef ESP8266
  Log.info("Free stack: %d", ESP.getFreeContStack());
#endif
  Log.info("Free heap: %d", ESP.getFreeHeap());

  Log.info("Setup 4: OTA");
  setupOta();

  Log.info("Setup 5: NTP");
  timeClient.begin();

  Log.info("Setup 6: tank_init");
  tank_init();

  Log.info("Setup 7: pump_init");
  pump_init();

  Log.info("Setup 8: server_init");
  server_init();

  Log.info("Setup complete, heap: %d", ESP.getFreeHeap());
}

void loop()
{
  static const char *step = "init";
  static unsigned long last_alive = 0;
  static bool first_heartbeat = true;

  // Heartbeat every 2 s: shows last completed step and heap so we can pinpoint a crash.
  if (millis() - last_alive >= 2000)
  {
    last_alive = millis();
    if (first_heartbeat)
    {
      // Repeat reset reason here — startup packet is often dropped while WiFi connects.
      Log.info("Reset reason: %s (heartbeat), heap: %d", g_reset_reason, ESP.getFreeHeap());
      first_heartbeat = false;
    }
    else
    {
      Log.info("Loop alive, step: %s, heap: %d", step, ESP.getFreeHeap());
    }
  }

#ifdef ESP32
  // Explicitly reset the Task WDT for the loop task.
  // delay() calls vTaskDelay() which yields to the scheduler but does NOT
  // call esp_task_wdt_reset() for this task's own WDT subscription.
  esp_task_wdt_reset();
#endif

#ifdef ESP8266
  step = "mDNS";
  MDNS.update();
#endif
  step = "OTA";
  ArduinoOTA.handle();
  step = "NTP";
  handleNtp();
  step = "tank";
  tank_handle();
  step = "server";
  server_handle();
  step = "pump";
  pump_handle();
  step = "delay";
  delay(10); // Yield to RTOS
}
