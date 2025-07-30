#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include "Log.h"
#include "settings.h" // Create from settings.template

#define TCP_SERVER_PORT 15011

inline void setupWifi()
{
  WiFi.disconnect();
  Serial.printf("Connecting WiFi to \"%s\"", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  //WiFi.softAPdisconnect();

  WiFi.onStationModeConnected([](const WiFiEventStationModeConnected &event) {
    Serial.printf("WiFi connected, RSSI: %d dBm", WiFi.RSSI());
  });

  WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
    Serial.printf("WiFi got IP, RSSI: %d dBm", WiFi.RSSI());
  });

  WiFi.begin(WIFI_SSID, WIFI_PASSKEY);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
}

inline void setupOta()
{
  ArduinoOTA.setHostname(APP_NAME);
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
