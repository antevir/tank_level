#pragma once

#include <arduino.h>
#include "Log.h"
#include "pump_state.h"

#define MDNS_REFRESH_INTERVAL_MS    (1000 * 60 * 15) // Each 15 min
#define PING_INTERVAL_MS            30000
#define PONG_TIMEOUT_MS             3000
#define RECONNECT_INTERVAL_MS       3000

class TankClient
{
public:
    void (*on_disconnect)(void) = NULL;
    void (*on_pump_state)(PumpState state) = NULL;

private:
    unsigned long last_tcp_heartbeat = 0;
    unsigned long last_tcp_reconnect_attempt = 0;
    bool awaiting_pong = false;
    bool was_disconnected = true;
    uint32_t last_lookup_time_ms = 0;
    WiFiClient client;
    IPAddress tank_ip;

    bool connect()
    {
        if (tank_ip == INADDR_NONE) {
            return false;
        }
        client.stop();
        Log.info("[CLIENT] Connecting to TCP server...");
        if (client.connect(this->tank_ip, TCP_SERVER_PORT)) {
            client.setNoDelay(true);
            Log.info("[CLIENT] Connected to server.");
            return true;
        } else {
            Log.warn("[CLIENT] Server connection failed.");
            return false;
        }
    }

    void handle_messages(void)
    {
        while (client.available())
        {
            String msg = client.readStringUntil('\n');
            msg.trim();
            Log.info("[CLIENT] Received: %s", msg.c_str());

            if (msg.startsWith("PUMP_STATE:")) {
                String state_str = msg.substring(strlen("PUMP_STATE:"));
                int state = state_str.toInt();
                if (on_pump_state)
                {
                    on_pump_state((PumpState)state);
                }
                Log.info("[CLIENT] Pump state updated: %d\n", state);
            } else if (msg == "PONG") {
                this->awaiting_pong = false;
            }
        }
    }


public:

    TankClient()
    {

    }

    bool send_pump_request(bool enable)
    {
        if (!client.connected())
        {
            Log.warn("[CLIENT] Not connected");
            return false;
        }

        if (tank_ip == INADDR_NONE) {
            Log.error("[CLIENT] tank_ip is invalid, skipping request.");
            return false;
        }

        client.println((enable ? "PUMP_ENABLE" : "PUMP_DISABLE"));

        return true;
    }

    void handle(void)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            if (this->was_disconnected && this->on_disconnect)
            {
                // TODO: Notify
                on_disconnect();
            }

            if (client.connected())
            {
                client.stop();
            }
            this->was_disconnected = true;
            return;
        }

        // Handle hostname lookup
        if (this->was_disconnected || (millis() - this->last_lookup_time_ms > MDNS_REFRESH_INTERVAL_MS))
        {
            IPAddress ip;

            Log.info("[CLIENT] Resolving tank.local...");
            WiFi.hostByName("tank.local", ip);
            if (ip != INADDR_NONE) {
                this->tank_ip = ip;
                Log.info("[CLIENT] tank.local resolved to: ");
                Serial.println(this->tank_ip);
            } else {
                Log.error("[CLIENT] Failed to resolve tank.local");
            }

            this->last_lookup_time_ms = millis();
        }
        this->was_disconnected = false;

        // Handle connection
        if (!client.connected())
        {
            if (millis() - this->last_tcp_reconnect_attempt >= RECONNECT_INTERVAL_MS)
            {
                this->last_tcp_reconnect_attempt = millis();
                connect();
                this->awaiting_pong = false;
            }
        }

        if (client.connected())
        {
            if (millis() - last_tcp_heartbeat >= PING_INTERVAL_MS)
            {
                this->last_tcp_heartbeat = millis();
                client.println("PING");
                Log.info("[CLIENT] PING sent");
            }
            this->handle_messages();

            if (this->awaiting_pong && millis() - this->last_tcp_heartbeat > PONG_TIMEOUT_MS)
            {
                Log.warn("[CLIENT] No PONG received, disconnecting...");
                client.stop();
                this->awaiting_pong = false;
            }
        }
    }

    /*void update(void)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            if (!was_disconnected)
            {
                led.set(LedOrange, LedBlinkSlow);
            }
            this->was_disconnected = true;
            return;
        }

        if (this->was_disconnected || (millis() - this->last_lookup_time_ms > MDNS_REFRESH_INTERVAL_MS))
        {
            Log.info("[CLIENT] Resolving tank.local...");

            IPAddress ip;
            WiFi.hostByName("tank.local", ip);
            if (ip != INADDR_NONE) {
                this->tank_ip = ip;
                Log.info("[CLIENT] tank.local resolved to: ");
                Serial.println(this->tank_ip);
            } else {
                Log.error("[CLIENT] Failed to resolve tank.local");
            }

            this->last_lookup_time_ms = millis();
        }

        this->was_disconnected = false;
    }*/

};