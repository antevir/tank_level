#include <arduino.h>
#include "common.h"
#include "Led.h"
#include "TankClient.h"

#define BUTTON_PIN      4
#define LED_GREEN_PIN   8
#define LED_RED_PIN     9

#define TICK_MS     100

static uint32_t ticks;
static Led led;
static TankClient tank_client;

static PumpState pump_state = PumpOff;


const int AirValue = 2960;   //you need to replace this value with Value_1
const int WaterValue = 610;  //you need to replace this value with Value_2
int soilMoistureValue = 0;
int soilmoisturepercent=0;

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
    Serial.begin(115200); // open serial port, set the baud rate to 9600 bps
    Log.info("Starting\n");

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    led.begin(LED_GREEN_PIN, LED_RED_PIN);
    Log.info("set\n");
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
}

void loop()
{
    /*int soilMoistureValue = analogRead(A0);  //put Sensor insert into soil
    Serial.println(soilMoistureValue);
    soilmoisturepercent = map(soilMoistureValue, AirValue, WaterValue, 0, 100);
    if (soilmoisturepercent >= 100)
    {
        Serial.println("100 %");
    }
    else if(soilmoisturepercent <=0)
    {
        Serial.println("0 %");
    }
    else if(soilmoisturepercent >0 && soilmoisturepercent < 100)
    {
        Serial.print(soilmoisturepercent);
        Serial.println("%");
    }*/

    static uint32_t last_millis = 0;

    ArduinoOTA.handle();
    tank_client.handle();

    uint32_t diff_ms = millis() - last_millis;
    if (diff_ms > TICK_MS) {
        // This is very rough, but we don't need high precision
        ticks = diff_ms / TICK_MS;
        last_millis = millis();
        led.update(ticks);
    }

}