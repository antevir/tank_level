#include <stdint.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include "common.h"
#include "pump_state.h"

#define BUTTON_PIN 4
#define TICK_MS     100

#define MDNS_REFRESH_INTERVAL_MS     (1000 * 60 * 15) // Each 15 min

#define UDP_PORT 15010

static WiFiClient client;
static PumpState pump_state = PumpOff;
static IPAddress tank_ip;
static WiFiUDP udp;
static IPAddress multicast_addr = IPAddress(224,3,29,72);

static bool check_button()
{
    static bool last_button_state = false;
    static int button_state_counter = 0;

    bool ret = false;
    bool button_state = digitalRead(BUTTON_PIN) == LOW;
    if (button_state)
    {
        // Button is pushed
        if (!last_button_state)
        {
            ret = true;
        }

        last_button_state = button_state;
        button_state_counter = 2;
    }
    else
    {
        if (last_button_state)
        {
            if (--button_state_counter <= 0)
            {
                last_button_state = false;
            }
        }
    }

    return ret;
}

static bool send_pump_request(bool enable)
{
    if (tank_ip == INADDR_NONE) {
        Log.error("tank_ip is invalid, skipping request.");
        return false;
    }

    bool ret;
    HTTPClient http;
    String url = "http://" + tank_ip.toString() + (enable ? "/enable_pump" : "/disable_pump");
    http.begin(client, url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    Log.info("url: %s\n", url.c_str());

    int code = http.POST(""); // Send empty body or replace with actual payload
    if (code > 0) {
        Log.info("POST succeeded: %d\n", code);
        ret = true;
    } else {
        Log.error("POST failed: %s\n", http.errorToString(code).c_str());
        ret = false;
    }
    http.end();

    return ret;
}

static void check_tank_ip(void) {
    static uint32_t last_lookup_time_ms = MDNS_REFRESH_INTERVAL_MS; // Make sure it triggers first time called

    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    if (millis() - last_lookup_time_ms > MDNS_REFRESH_INTERVAL_MS)
    {
        Log.info("Resolving tank.local...");

        IPAddress ip;
        WiFi.hostByName("tank.local", ip);
        if (ip != INADDR_NONE) {
            tank_ip = ip;
            Log.info("tank.local resolved to: ");
            Serial.println(tank_ip);
        } else {
            Log.error("Failed to resolve tank.local");
        }

        last_lookup_time_ms = millis();
    }
}

void check_udp_data(void)
{
    int packet_size = udp.parsePacket();
    if (packet_size > 0)
    {
        char buf[255];
        int len = udp.read(buf, 254);
        if (len > 0) {
            buf[len] = 0;  // Null terminate

            char name[64];
            int value;

            if (sscanf(buf, "%63[^:]:%d", name, &value) == 2) {
                Log.info("Rx name: %s, value: %d\n", name, value);
                if (strcmp(name, "PUMP_STATE") == 0)
                {
                    pump_state = (PumpState)value;
                }
            } else {
                Log.warn("Invalid format in UDP message.");
            }
        }
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    Log.begin();

    // Connect to WiFi network
    setupWifi();

    if (!MDNS.begin(APP_NAME))
    {
        Log.error("Error setting up MDNS responder!");
    }
    Log.info("Free stack: %d", ESP.getFreeContStack());

    setupOta();
}

void loop()
{
    static IPAddress last_ip;
    static uint32_t last_millis = 0;

    MDNS.update();
    ArduinoOTA.handle();

    if (WiFi.status() == WL_CONNECTED)
    {
        if (WiFi.localIP() != last_ip)
        {
            last_ip = WiFi.localIP();
            Log.info("Joining multicast");
            udp.beginMulticast(last_ip, multicast_addr, UDP_PORT);  // rejoin group
        }
        check_udp_data();
    }

    uint32_t diff_ms = millis() - last_millis;
    if (diff_ms > TICK_MS) {
        // This is very rough, but we don't need high precision
        last_millis = millis();

        if (check_button())
        {
            Log.info("Button pressed");
            switch (pump_state)
            {
                case PumpRunning:
                    send_pump_request(false);
                    break;
                default:
                    send_pump_request(true);
                    break;
            }
        }
        check_tank_ip();
    }
}
