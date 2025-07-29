#include <NTPClient.h>
#include <WiFiUdp.h>

#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include "MedianFilterLib.h"

#include "common.h"
#include "pins.h"
#include "tank.h"
#include "server.h"
#include "pump.h"

static WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_CLOCK_OFFSET, 60000);

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
    timeClient.update();
    Log.info("Current time: %s", timeClient.getFormattedTime().c_str());
    setTime(timeClient.getEpochTime());
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(DIST_ECHO_PIN, INPUT);
  pinMode(DIST_TRIG_PIN, OUTPUT);
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  pinMode(SDCARD_CS_PIN, OUTPUT);
  pinMode(GOT_WATER_PIN, INPUT_PULLUP);

  digitalWrite(DIST_TRIG_PIN, LOW);
  digitalWrite(PUMP_RELAY_PIN, HIGH);
  digitalWrite(SDCARD_CS_PIN, HIGH);

  Log.begin();

  // Connect to WiFi network
  setupWifi();

  Log.info("Initializing SD card...");
  if (SD.begin(SDCARD_CS_PIN))
  {
    Log.info("initialization done.");
  }
  else
  {
    Log.error("initialization failed!");
  }

  if (!MDNS.begin(APP_NAME))
  {
    Log.error("Error setting up MDNS responder!");
  }
  MDNS.addService("http", "tcp", 80);

  Log.info("Free stack: %d", ESP.getFreeContStack());

  setupOta();

  tank_init();
  pump_init();
  server_init();
}

void loop()
{
  MDNS.update();
  ArduinoOTA.handle();
  handleNtp();
  tank_handle();
  server_handle();
  pump_handle();
}
