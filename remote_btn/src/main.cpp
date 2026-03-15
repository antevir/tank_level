#include <stdint.h>
#include "common.h"
#include "Led.h"
#include "TankClient.h"

#ifdef FEATURE_NIGHTLIGHT
#include "nightlight_server.h"
#endif

#define BUTTON_PIN      4  // D2
#ifdef FEATURE_NIGHTLIGHT
# define LED_GREEN_PIN   2  // D4 (onboard blue LED shares this pin)
# define LED_RED_PIN     0  // D3
#else
# define LED_GREEN_PIN   12 // D6
# define LED_RED_PIN     15 // D8
#endif

#define TICK_MS     100

#define UDP_PORT 15010

static uint32_t ticks;
static Led led;
static TankClient tank_client;

static PumpState pump_state = PumpOff;

#ifdef FEATURE_NIGHTLIGHT
static Nightlight nightlight;
static NightlightServer nl_server;
#endif

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
    pump_state = state;
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
    Log.info("Free stack: %d", ESP.getFreeContStack());

    setupOta();

#ifdef FEATURE_NIGHTLIGHT
    nightlight.begin();
    nl_server.begin(&nightlight);
    Log.info("Nightlight feature enabled");
#endif
}

void loop()
{
    static IPAddress last_ip;
    static uint32_t last_millis = 0;

    MDNS.update();
    ArduinoOTA.handle();
    tank_client.handle();

#ifdef FEATURE_NIGHTLIGHT
    nl_server.handle();
#endif

    uint32_t diff_ms = millis() - last_millis;
    if (diff_ms > TICK_MS) {
        // This is very rough, but we don't need high precision
        ticks += diff_ms / TICK_MS;
        last_millis = millis();

        if (check_button())
        {
            Log.info("Button pressed");
            switch (pump_state)
            {
                case PumpRunning:
                case PumpIdle:
                    tank_client.send_pump_request(false);
                    break;
                default:
                    tank_client.send_pump_request(true);
                    break;
            }
        }
        led.update(ticks);

#ifdef FEATURE_NIGHTLIGHT
        nightlight.sampleADC();
        nightlight.update();
#endif
    }
}
