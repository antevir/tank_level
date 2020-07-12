#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <SPI.h>
#include <SD.h>
#include <TimeLib.h>
#include "MedianFilterLib.h"

#include "Log.h"
#include "pins.h"
#include "tank.h"
#include "server.h"
#include "pump.h"

#include "settings.h" // Create from settings.template

static WiFiUDP ntpUDP;

// By default 'pool.ntp.org' is used with 60 seconds update interval and
// no offset
NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_CLOCK_OFFSET, 60000);

static void connectWifi()
{
  WiFi.disconnect();
  Log.info("Connecting WiFi to \"%s\"", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  //WiFi.softAPdisconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSKEY);

  // Wait for connection
  for (int i = 0; i < 25; i++)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      delay(250);
      Serial.print(".");
      delay(250);
    }
  }
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED)
  {
    Log.info("WiFi connected");
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    timeClient.update();
  }
  else
  {
    Log.error("WiFi connect FAILED");
  }
}

static void setup_ota()
{
  ArduinoOTA.setHostname("tank");
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif

  ArduinoOTA.onStart([]() {
    Log.info("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Log.info("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static int last_percent = 0;
    int percent = (progress / (total / 100));
    int diff = percent - last_percent;
    diff = diff < 0 ? -diff : diff;
    if (diff >= 5)
    {
      last_percent = percent;
      Log.info("Progress: %u%%\r", (progress / (total / 100)));
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Log.error("Error[%u]:", error);
    switch (error)
    {
    case OTA_AUTH_ERROR:
      Log.error("Auth Failed");
      break;
    case OTA_BEGIN_ERROR:
      Log.error("Begin Failed");
      break;
    case OTA_CONNECT_ERROR:
      Log.error("Connect Failed");
      break;
    case OTA_RECEIVE_ERROR:
      Log.error("Receive Failed");
      break;
    case OTA_END_ERROR:
      Log.error("End Failed");
      break;
    }
  });
  ArduinoOTA.begin();
}

void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(DIST_ECHO_PIN, INPUT);
  pinMode(DIST_TRIG_PIN, OUTPUT);
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  pinMode(SDCARD_CS_PIN, OUTPUT);

  digitalWrite(DIST_TRIG_PIN, LOW);
  digitalWrite(PUMP_RELAY_PIN, HIGH);
  digitalWrite(SDCARD_CS_PIN, HIGH);

  Log.begin();

  // Connect to WiFi network
  connectWifi();

  Log.info("Initializing SD card...");
  if (SD.begin(SDCARD_CS_PIN))
  {
    Log.info("initialization done.");
  }
  else
  {
    Log.error("initialization failed!");
  }

  timeClient.update();
  Log.info("Current time: %s", timeClient.getFormattedTime().c_str());
  setTime(timeClient.getEpochTime());

  // Print the IP address
  Log.info("URL: http://%s", WiFi.localIP().toString().c_str());

  if (!MDNS.begin("tank"))
  { // Start the mDNS responder for esp8266.local
    Log.error("Error setting up MDNS responder!");
  }
  MDNS.addService("http", "tcp", 80);

  Log.info("Free stack: %d", ESP.getFreeContStack());

  setup_ota();

  tank_init();
  pump_init();
  server_init();
}

void loop()
{
  MDNS.update();
  ArduinoOTA.handle();
  if (WiFi.status() != WL_CONNECTED)
  {
    connectWifi();
  }
  tank_handle();
  server_handle();
  pump_handle();
}
