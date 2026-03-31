#include <stdint.h>
#include "common.h"
#include "Led.h"
#include "TankClient.h"
#include "greenhouse_server.h"

#define LED_GREEN_PIN   16
#define LED_RED_PIN     18
#define BUTTON_PIN      33

#define TICK_MS     100

static uint32_t ticks;
static Led led;
static TankClient tank_client;

// Exposed to GreenhouseServer via extern for UI indicators
volatile PumpState g_pump_state    = PumpOff;
volatile bool     g_button_pressed = false;
volatile bool     g_tank_connected = false;

static GreenhouseCtrl greenhouse;
static GreenhouseServer gh_server;

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

static void disconnect_cb(void)
{
    led.set(LedOrange, LedBlinkSlow);
}

static void pump_state_cb(PumpState state)
{
    g_pump_state = state;
    greenhouse.notifyPumpState(state);
    switch (state)
    {
        case PumpOff:
            led.set(LedOff, LedBlinkOff);
            break;
        case PumpDryRun:
            led.set(LedRed, LedBlinkOff);
            break;
        case PumpIdle:
            led.set(LedGreen, LedBlinkOff);
            break;
        case PumpRunning:
            led.set(LedGreen, LedBlinkFast);
            break;
        case PumpWarning:
            led.set(LedOrange, LedBlinkFast);
            break;
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    led.begin(LED_GREEN_PIN, LED_RED_PIN);
    led.set(LedOrange, LedBlinkSlow);
    tank_client.on_disconnect = disconnect_cb;
    tank_client.on_pump_state = pump_state_cb;

    Log.begin();

    // Connect to WiFi network
    setupWifi();

    if (!MDNS.begin(APP_NAME))
    {
        Log.error("Error setting up MDNS responder!");
    }

    setupOta();

    greenhouse.begin();
    greenhouse.on_pump_request = [](bool enable) { tank_client.send_pump_request(enable); };
    gh_server.begin(&greenhouse);
    Log.info("Greenhouse started");
}

void loop()
{
    static uint32_t last_millis = 0;

    ArduinoOTA.handle();
    tank_client.handle();
    g_tank_connected = tank_client.isConnected();
    gh_server.handle();

    uint32_t diff_ms = millis() - last_millis;
    if (diff_ms > TICK_MS) {
        ticks += diff_ms / TICK_MS;
        last_millis = millis();

        g_button_pressed = (digitalRead(BUTTON_PIN) == LOW);

        if (check_button())
        {
            Log.info("Button pressed");
            bool pump_on;
            switch (g_pump_state)
            {
                case PumpRunning:
                case PumpIdle:
                    pump_on = false;
                    break;
                default:
                    pump_on = true;
                    break;
            }
            greenhouse.userPumpToggle(pump_on);
        }
        led.update(ticks);
        greenhouse.update();
    }
}
