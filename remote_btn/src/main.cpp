#include <stdint.h>
#include "common.h"
#include "pump_state.h"

#define BUTTON_PIN      4
#define LED_GREEN_PIN   12
#define LED_RED_PIN     15

#define TICK_MS     100

#define LED_BLINK_FAST_TICK 5
#define LED_BLINK_SLOW_TICK 20

#define MDNS_REFRESH_INTERVAL_MS     (1000 * 60 * 15) // Each 15 min

#define UDP_PORT 15010

#define PING_INTERVAL_MS 30000
#define PONG_TIMEOUT_MS 3000
#define RECONNECT_INTERVAL_MS 3000

enum LedColor
{
    LedOff = 0,
    LedGreen,
    LedRed,
    LedOrange
};

enum LedBlink
{
    LedBlinkOff = 0,
    LedBlinkSlow,
    LedBlinkFast
};

typedef struct {
    LedColor color;
    LedBlink blink;
    uint32_t last_blink_tick;
    bool blink_state;
} led_state_t;

static uint32_t ticks;
static led_state_t led_state;
static WiFiClient client;
static bool awaiting_pong = false;
static PumpState pump_state = PumpOff;
static IPAddress tank_ip;
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

static void set_led(LedColor color, LedBlink blink)
{
    led_state.blink_state = true;
    led_state.color = color;
    led_state.blink = blink;
    led_state.last_blink_tick = ticks;
}

static void update_led(void)
{
    LedColor led_color = led_state.color;
    if (led_state.blink != LedBlinkOff)
    {
        uint32_t blink_tick = (led_state.blink == LedBlinkFast) ?
                               LED_BLINK_FAST_TICK : LED_BLINK_SLOW_TICK;
        if (ticks - led_state.last_blink_tick > blink_tick)
        {
            led_state.last_blink_tick = ticks;
            led_state.blink_state = !led_state.blink_state;
        }
        if (!led_state.blink_state)
        {
            led_color = LedOff;
        }
    }
    bool red = led_color == LedRed;
    bool green = led_color == LedGreen;
    if (led_color == LedOrange)
    {
        red = true;
        green = true;
    }
    digitalWrite(LED_RED_PIN, red ? HIGH : LOW);
    digitalWrite(LED_GREEN_PIN, green ? HIGH : LOW);
}

static void update_pump_state(PumpState state)
{
    pump_state = state;
    switch (state)
    {
        case PumpOff:
            set_led(LedOff, LedBlinkOff);
            break;
        case PumpDryRun:
            set_led(LedRed, LedBlinkOff);
            break;
        case PumpIdle:
            set_led(LedGreen, LedBlinkOff);
            break;
        case PumpRunning:
            set_led(LedGreen, LedBlinkFast);
            break;
        case PumpWarning:
            set_led(LedOrange, LedBlinkFast);
            break;
    }
}

static bool tcp_client_connect()
{
    if (tank_ip == INADDR_NONE) {
        return false;
    }
    client.stop();
    Log.info("Connecting to TCP server...");
    if (client.connect(tank_ip, TCP_SERVER_PORT)) {
        client.setNoDelay(true);
        Log.info("Connected to server.");
        return true;
    } else {
        Log.warn("Server connection failed.");
        return false;
    }
}

static void tcp_client_handle_messages(void)
{
    while (client.available())
    {
        String msg = client.readStringUntil('\n');
        msg.trim();
        Log.info("[CLIENT] Received: %s", msg.c_str());

        if (msg.startsWith("PUMP_STATE:")) {
            String stateStr = msg.substring(strlen("PUMP_STATE:"));
            int state = stateStr.toInt();
            update_pump_state((PumpState)state);
            Log.info("[CLIENT] Pump state updated: %d\n", state);
        } else if (msg == "PONG") {
            awaiting_pong = false;
        }
    }
}

void tcp_client_loop()
{
    static unsigned long last_tcp_heartbeat = 0;
    static unsigned long last_tcp_reconnect_attempt = 0;

    if (WiFi.status() != WL_CONNECTED)
    {
        if (client.connected())
        {
            client.stop();
        }
        return;
    }

    if (!client.connected())
    {
        if (millis() - last_tcp_reconnect_attempt >= RECONNECT_INTERVAL_MS)
        {
            last_tcp_reconnect_attempt = millis();
            tcp_client_connect();
            awaiting_pong = false;
        }
    }

    if (client.connected())
    {
        if (millis() - last_tcp_heartbeat >= PING_INTERVAL_MS)
        {
            last_tcp_heartbeat = millis();
            client.println("PING");
            Log.info("[CLIENT] PING sent");
        }
        tcp_client_handle_messages();

        if (awaiting_pong && millis() - last_tcp_heartbeat > PONG_TIMEOUT_MS)
        {
            Serial.println("[CLIENT] No PONG received, disconnecting...");
            client.stop();
            awaiting_pong = false;
        }
    }
}

static bool send_pump_request(bool enable)
{
    if (!client.connected())
    {
        Log.warn("[CLIENT] Not connected");
        return false;
    }

    if (tank_ip == INADDR_NONE) {
        Log.error("tank_ip is invalid, skipping request.");
        return false;
    }

    client.println((enable ? "PUMP_ENABLE" : "PUMP_DISABLE"));

    return true;
}

static void check_tank_ip(void)
{
    static bool was_disconnected = true;
    static uint32_t last_lookup_time_ms = 0;

    if (WiFi.status() != WL_CONNECTED)
    {
        if (!was_disconnected)
        {
            set_led(LedOrange, LedBlinkSlow);
        }
        was_disconnected = true;
        return;
    }

    if (was_disconnected || (millis() - last_lookup_time_ms > MDNS_REFRESH_INTERVAL_MS))
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

    was_disconnected = false;
}

void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_RED_PIN, OUTPUT);
    set_led(LedOrange, LedBlinkSlow);

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
    tcp_client_loop();

    uint32_t diff_ms = millis() - last_millis;
    if (diff_ms > TICK_MS) {
        // This is very rough, but we don't need high precision
        ticks = diff_ms / TICK_MS;
        last_millis = millis();

        if (check_button())
        {
            Log.info("Button pressed");
            switch (pump_state)
            {
                case PumpRunning:
                case PumpIdle:
                    send_pump_request(false);
                    break;
                default:
                    send_pump_request(true);
                    break;
            }
        }
        check_tank_ip();
        update_led();
    }
}
