#include <Arduino.h>
#include <TimeLib.h>
#include "MedianFilterLib.h"
#include <ESP8266WiFi.h>
#include "Log.h"
#include "pins.h"
#include "pump.h"

#define PUMP_ENABLE_TIME_S (60 * 15)

#define PUMP_ON_CURRENT_THRESHOLD_MA 800
#define PUMP_DRYRUN_THRESHOLD_MA 2000
#define PUMP_DRYRUN_MIN_TIME_S 30

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define FILTER_LEN 5
#define SAMPLE_INTERVAL_MS 100

// 1.5V from ACS712 = 10A, V divider is 1/2.5 => 10A = 1.5 * (1/2.5) = 0.6V at A0 = ADC value ~= 614
// However, voltage is inverted on A0 => ADC value: 1023-614 = 409 = 10A
// => mA = (1023 - ADC value) * 10A * 1000 / 409
// Please note that ADC value is inverted, i.e. 1023 = 0 mA
#define ADC_TO_mA(x) ((1024 - (x)) * (10000.0f / 409.0f))

enum PumpState
{
    PumpDryRun = -2,
    PumpOff = -1,
    PumpIdle = 0,
    PumpRunning,
    PumpWarning
};

static int last_second = 0;
static unsigned long last_sample_time;
static MedianFilter<int> medianFilter(FILTER_LEN);
static PumpState pump_state = PumpIdle;
static int enable_timer;
static bool pump_enabled;

static int warning_pattern[] = {0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, -1};
static int *warning_ptr;

static String get_state_string(PumpState state)
{
    switch (state)
    {
    case PumpIdle:
        return "Idle";
    case PumpRunning:
        return "Running";
    case PumpWarning:
        return "Warning";
    case PumpDryRun:
        return "Dry Run";
    case PumpOff:
        return "Off";
    }
    return "Unkown";
}

static bool take_sample()
{
    static int filter_entries = 0;
    int sample = analogRead(CURRENT_ADC_PIN);

    medianFilter.AddValue(sample);
    if (filter_entries < FILTER_LEN)
    {
        filter_entries++;
    }
    if (filter_entries == FILTER_LEN)
    {
        return true;
    }
    return false;
}

static bool enable_pump(bool enable)
{
    bool toggled = pump_enabled != enable;
    if (toggled)
    {
        enable_timer = PUMP_ENABLE_TIME_S;
    }
    digitalWrite(PUMP_RELAY_PIN, enable ? LOW : HIGH);
    pump_enabled = enable;
    return toggled;
}

static PumpState enter_state(PumpState state)
{
    Log.info("Entering pump state: %s", get_state_string(state).c_str());
    switch (state)
    {
    case PumpIdle:
        enable_pump(true);
        break;
    case PumpRunning:
        enable_pump(true);
        break;
    case PumpWarning:
        warning_ptr = &warning_pattern[0];
        enable_pump(*warning_ptr);
        break;
    case PumpDryRun:
    case PumpOff:
        enable_pump(false);
        break;
    }
    return state;
}

static PumpState execute_warning(bool pump_active)
{
    warning_ptr++;
    if (*warning_ptr < 0)
    {
        return enter_state(PumpOff);
    }
    bool pump_toggled = enable_pump(*warning_ptr);
    if (!pump_toggled && pump_enabled && !pump_active)
    {
        return enter_state(PumpIdle);
    }
    return PumpWarning;
}

static PumpState execute_state(PumpState state)
{
    if (enable_timer > 0)
        enable_timer--;

    bool pump_active = pump_is_on();
    bool timeout = enable_timer <= 0;

    switch (state)
    {
    case PumpIdle:
        if (pump_active)
            return enter_state(PumpRunning);
        if (timeout)
            return enter_state(PumpOff);
        break;
    case PumpRunning:
        if (!pump_active)
            return enter_state(PumpIdle);
        if (timeout)
            return enter_state(PumpWarning);
        break;
    case PumpWarning:
        return execute_warning(pump_active);
    default:
        break;
    }
    return state;
}

static void check_for_dryrun()
{
    static int dryrun_counter = 0;
    if (!pump_is_on())
    {
        dryrun_counter = 0;
        return;
    }
    if (pump_get_current_mA() < PUMP_DRYRUN_THRESHOLD_MA)
    {
        if (dryrun_counter++ > PUMP_DRYRUN_MIN_TIME_S)
        {
            pump_state = enter_state(PumpDryRun);
        }
    }
}

static void check_button()
{
    static bool last_button_state = false;
    static int button_state_counter = 0;

    bool button_state = digitalRead(BUTTON_PIN) == LOW;
    if (button_state)
    {
        // Button is pushed
        if (!last_button_state)
        {
            if (pump_state >= PumpIdle)
            {
                pump_disable();
            }
            else
            {
                pump_enable();
            }
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
}

void pump_init()
{
    last_second = second();
    last_sample_time = millis();
    take_sample();
    pump_state = enter_state(PumpOff);
}

void pump_enable()
{
    pump_state = enter_state(PumpIdle);
}

void pump_disable()
{
    pump_state = enter_state(PumpOff);
}

int pump_get_current_mA()
{
    int value = medianFilter.GetFiltered();
    return ADC_TO_mA(value);
}

bool pump_is_on()
{
    return pump_get_current_mA() > PUMP_ON_CURRENT_THRESHOLD_MA;
}

String pump_get_stats_json()
{
    String current = String(pump_get_current_mA());
    String active = pump_is_on() ? "1" : "0";
    String stateTxt = get_state_string(pump_state);
    String state = String(pump_state);
    return "{\"CUR\":" + current + ",\"ACTIVE\":" + active + ",\"STATE\":" + state + ",\"STATETEXT\":\"" + stateTxt + "\"}";
}

void pump_handle()
{
    static bool filter_filled = false;
    if (millis() - last_sample_time > SAMPLE_INTERVAL_MS)
    {
        last_sample_time = millis();
        filter_filled = take_sample();
        check_button();
    }

    if (last_second != second())
    {
        last_second = second();
        check_for_dryrun();
        if (filter_filled)
        {
            pump_state = execute_state(pump_state);
        }
    }
}
