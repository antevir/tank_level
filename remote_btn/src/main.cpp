#include <stdint.h>
#include "common.h"
#include "Led.h"
#include "TankClient.h"

#ifdef FEATURE_GREENHOUSE
# include "greenhouse_server.h"
#endif

#ifdef FEATURE_GREENHOUSE
# define LED_GREEN_PIN   16
# define LED_RED_PIN     18
# define BUTTON_PIN      33
#else
# define BUTTON_PIN      4  // D2
# define LED_GREEN_PIN   12 // D6
# define LED_RED_PIN     15 // D8
#endif

#define TICK_MS     100

#define UDP_PORT 15010

static uint32_t ticks;
static Led led;
static TankClient tank_client;

// Exposed to GreenhouseServer via extern for UI indicators
volatile PumpState g_pump_state    = PumpOff;
volatile bool     g_button_pressed = false;
volatile bool     g_tank_connected = false;

#ifdef FEATURE_GREENHOUSE
static GreenhouseCtrl greenhouse;
static GreenhouseServer gh_server;
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
    g_pump_state = state;
#ifdef FEATURE_GREENHOUSE
    greenhouse.notifyPumpState(state);
#endif
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

#ifdef ESP8266
    // Disable modem sleep so the radio stays on permanently.
    // By default ESP8266 powers the radio down between DTIM beacon intervals
    // (typically ~1 s on most APs).  On each wakeup the SDK disables timer1
    // interrupts briefly to resync with the AP — timer1 is also the engine
    // behind analogWrite() software PWM, so every wakeup causes a visible
    // PWM glitch.  WIFI_NONE_SLEEP keeps the radio always-on and eliminates
    // these periodic disruptions at the cost of ~20 mA extra idle current.
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif

    if (!MDNS.begin(APP_NAME))
    {
        Log.error("Error setting up MDNS responder!");
    }
#ifdef ESP8266
    Log.info("Free stack: %d", ESP.getFreeContStack());
#endif

    setupOta();

#ifdef FEATURE_GREENHOUSE
    greenhouse.begin();
    greenhouse.on_pump_request = [](bool enable) { tank_client.send_pump_request(enable); };
    gh_server.begin(&greenhouse);
    Log.info("Greenhouse feature enabled");
#endif
}

void loop()
{
    static IPAddress last_ip;
    static uint32_t last_millis = 0;

#ifdef ESP8266
    MDNS.update();  // ESP32 MDNS runs automatically; no update() needed
#endif
    ArduinoOTA.handle();
    tank_client.handle();
    g_tank_connected = tank_client.isConnected();

#ifdef FEATURE_GREENHOUSE
    gh_server.handle();
#endif

    uint32_t diff_ms = millis() - last_millis;
    if (diff_ms > TICK_MS) {
        // This is very rough, but we don't need high precision
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
#ifdef FEATURE_GREENHOUSE
            greenhouse.userPumpToggle(pump_on);
#else
            tank_client.send_pump_request(pump_on);
#endif
        }
        led.update(ticks);

#ifdef FEATURE_GREENHOUSE
        greenhouse.update();
#endif
    }
}
