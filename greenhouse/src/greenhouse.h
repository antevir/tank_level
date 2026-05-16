#pragma once

#include <Arduino.h>
#include <time.h>
#ifdef ESP32
# include <esp_sntp.h>
#else
# include <coredecls.h>
#endif

#include "greenhouse_config.h"
#include "nexa.h"
#include "pump_state.h"
#include "Log.h"

/*
 * Photo resistor circuit:
 *   - LDR: unknown type, ~60kΩ in darkness, ~2kΩ in ambient room light
 *   - Wiring: LDR from 3.3V to LDR_PIN, 10kΩ from LDR_PIN to GND
 *   - Higher ADC reading = more light, lower = darker
 *
 * Capacitive soil moisture sensor:
 *   - Analog amplifier output connected to MOISTURE_PIN (GPIO 9), ADC1
 *   - ~1 V in water (~310 ADC), ~2.5 V in dry air (~775 ADC)
 *   - Higher ADC = drier soil
 *   - 1MΩ external pulldown: disconnected pin reads ~0 ADC
 *
 * Both output channels are digital on/off (no PWM).
 * Both outputs drive relay modules (active-low coil input):
 *   - LIGHT_PIN:  relay → lamp
 *   - VALVE_PIN:  relay → irrigation solenoid valve
 */

// --- Pin definitions ---
#define LDR_PIN         3       // Light sensor (ADC)
#define MOISTURE_PIN    9       // Soil moisture sensor (ADC via amplifier)
#define LIGHT_PIN       7       // Digital output: lamp relay
#define VALVE_PIN       35      // Digital output: irrigation valve relay

// When the moisture sensor is disconnected the 1MΩ external pulldown holds GPIO9 near GND → ~0 ADC.
// The sensor minimum (submerged in water) reads ~310 ADC, so 50 is a very safe disconnect threshold.
#define MOISTURE_DISCONNECT_ADC  50

// Fixed wet-end calibration: anything at or below this ADC value is treated as 100% moisture.
// The sensor in water reads ~310 ADC which is already well above this, so the clamping in
// adcToPercent() gives 100% for any reading that is wetter than real-world wet soil.
#define MOISTURE_CAL_WET  100

// Relays are active-low: pull IN pin LOW to energise coil
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// --- Timing ---
#define HISTORY_SIZE        1440    // 24h at 1 sample per minute
#define HISTORY_INTERVAL_MS 60000   // 1 minute
#define SENSOR_AVG_MS       1000    // Read sensors every 1 second

// Stockholm timezone
#define GH_TIME_ZONE "CET-1CEST,M3.5.0,M10.5.0/3"

// --- NTP sync callback ---
static volatile bool g_gh_time_synced = false;

#ifdef ESP32
static void ghTimeSyncCb(struct timeval *)
#else
static void ghTimeSyncCb()
#endif
{
    time_t now = time(nullptr);
    if (now > 1000000000UL)
        g_gh_time_synced = true;
}

class GreenhouseCtrl {
public:
    GreenhouseConfig config;
    NexaController    nexa;

    // --- Pump control callback (set by main.cpp) ---
    void (*on_pump_request)(bool enable) = nullptr;

    // --- Light sensor state ---
    uint16_t ldr_reading = 0;
    uint16_t ldr_history[HISTORY_SIZE];
    uint16_t ldr_history_count     = 0;
    uint16_t ldr_history_write_idx = 0;
    bool     light_on  = false;
    bool     is_dark   = false;

    // --- Moisture sensor state ---
    uint16_t moisture_reading    = 0;     // Raw ADC (0-1023)
    uint8_t  moisture_pct        = 0;     // Calibrated 0-100%
    bool     moisture_sensor_ok  = true;  // false when ADC > MOISTURE_DISCONNECT_ADC
    uint16_t moisture_history[HISTORY_SIZE];  // Stores raw ADC
    uint16_t moisture_history_count     = 0;
    uint16_t moisture_history_write_idx = 0;

    // --- Irrigation state (delegated to IrrigationCtrl) ---
    IrrigationCtrl m_irr;
    bool           valve_on() const { return m_irr.valve_on; }
    IrrigationState irr_state() const { return m_irr.state; }
    uint8_t        irr_cycle_count() const { return m_irr.cycle_count; }

    bool isTimeSynced()  const { return g_gh_time_synced; }
    bool isTankEmpty()   const { return m_tank_empty; }

    // Called from main.cpp when the LOCAL user presses the pump button.
    // pump_on: true = user wants pump ON, false = user wants pump OFF.
    void userPumpToggle(bool pump_on)
    {
        m_external_pump_on = pump_on;
        if (on_pump_request)
            on_pump_request(pump_on);

        if (!pump_on)
        {
            // Mark as user-aborted so the active TP window does not immediately
            // reopen the valve and restart the pump.
            // The abort stays set until the TP window ends naturally (evaluateSchedule
            // clears it when should_be_on goes false) — a second button press to
            // restart the pump does NOT re-enable the current window.
            m_user_aborted = true;
            pumpForcedOff(millis());
        }
    }

    // Called from pump_state_cb whenever the tank server reports a new state.
    // Detects external pump activation (another remote_btn or tank button)
    // and external pump shutoff (timeout, dry-run, other user).
    void notifyPumpState(PumpState state)
    {
        m_tank_empty = (state == PumpDryRun);  // Block irrigation when tank is empty
        bool pump_on = (state >= PumpIdle);    // Idle, Running, Warning = pump enabled
        if (pump_on && !m_irr_pump_on)
        {
            m_external_pump_on = true;
        }
        else if (!pump_on)
        {
            m_external_pump_on = false;
            m_irr_pump_on = false;
            pumpForcedOff(millis());
        }
    }

    void begin()
    {
        config.begin();

#ifdef ESP32
        analogReadResolution(10);           // 0-1023 to match all thresholds / UI
        analogSetAttenuation(ADC_11db);     // Full-scale ~3.9 V; sensor outputs up to ~2.5 V
#endif

        pinMode(LDR_PIN, INPUT);
        pinMode(MOISTURE_PIN, INPUT);  // 1MΩ external pulldown: sensor absent → ~0 ADC
        pinMode(LIGHT_PIN, OUTPUT);
        pinMode(VALVE_PIN, OUTPUT);
        digitalWrite(LIGHT_PIN, RELAY_OFF);  // Relays start de-energised
        digitalWrite(VALVE_PIN, RELAY_OFF);

        memset(ldr_history, 0, sizeof(ldr_history));
        memset(moisture_history, 0, sizeof(moisture_history));

        // NTP setup (Stockholm timezone)
#ifdef ESP32
        sntp_set_time_sync_notification_cb(ghTimeSyncCb);
        configTzTime(GH_TIME_ZONE, "pool.ntp.org", "time.nist.gov");
#else
        settimeofday_cb(ghTimeSyncCb);
        configTime(GH_TIME_ZONE, "pool.ntp.org", "time.nist.gov");
#endif

        nexa.init();  // Start mDNS for Nexa plug discovery

        Log.info("[GH] Greenhouse controller initialized");
    }

    // Called from loop tick (~100 ms)
    void update()
    {
        unsigned long now_ms = millis();

        // --- Read sensors once per second ---
        if (now_ms - m_last_avg_ms >= SENSOR_AVG_MS)
        {
            m_last_avg_ms    = now_ms;
            ldr_reading        = analogRead(LDR_PIN);
            moisture_reading   = analogRead(MOISTURE_PIN);
            moisture_sensor_ok = (moisture_reading >= MOISTURE_DISCONNECT_ADC);
            moisture_pct       = moisture_sensor_ok
                                 ? adcToPercent(moisture_reading,
                                                MOISTURE_CAL_WET,
                                                config.data.irrigation.moisture_cal_dry)
                                 : 0;
            m_sensor_ready     = true;
        }

        // --- Store history every minute ---
        if (now_ms - m_last_history_ms >= HISTORY_INTERVAL_MS)
        {
            m_last_history_ms = now_ms;
            if (m_sensor_ready)
            {
                ldr_history[ldr_history_write_idx] = ldr_reading;
                ldr_history_write_idx = (ldr_history_write_idx + 1) % HISTORY_SIZE;
                if (ldr_history_count < HISTORY_SIZE)
                    ldr_history_count++;

                moisture_history[moisture_history_write_idx] = moisture_reading;
                moisture_history_write_idx = (moisture_history_write_idx + 1) % HISTORY_SIZE;
                if (moisture_history_count < HISTORY_SIZE)
                    moisture_history_count++;
            }
        }

        if (!m_sensor_ready) return;

        evaluateLight(now_ms);
        evaluateIrrigation(now_ms);
        nexa.update(config.data.nexa, ldr_reading, light_on,
                    config.data.light.twilight_lamp_offset,
                    g_gh_time_synced);
    }

    // Calibrate dry point: record current reading as 0% (sensor in dry air)
    void calibrateDry()
    {
        config.data.irrigation.moisture_cal_dry = moisture_reading;
        config.save();
        moisture_pct = adcToPercent(moisture_reading,
                                    MOISTURE_CAL_WET,
                                    config.data.irrigation.moisture_cal_dry);
        Log.info("[GH] Moisture cal dry: %d ADC", moisture_reading);
    }

private:
    bool           m_sensor_ready     = false;
    unsigned long  m_last_avg_ms      = 0;
    unsigned long  m_last_history_ms  = 0;
    bool           m_irr_pump_on      = false;  // Irrigation requested pump on
    bool           m_external_pump_on = false;  // Pump on due to user (local or remote button)
    bool           m_tank_empty       = false;  // true when tank reports PumpDryRun
    bool           m_user_aborted     = false;  // true when user stopped pump during active TP

    // LDR debounce: track when reading first crossed the threshold
    unsigned long  m_ldr_change_since_ms = 0;   // When LDR first crossed threshold

    // Debounce: LDR must stay on the other side of the threshold for this long
    static constexpr unsigned long LDR_DEBOUNCE_MS = 60UL * 1000UL;  // 60 seconds

    // --- Light evaluation (CH1): digital on/off ---
    void evaluateLight(unsigned long now_ms)
    {
        const LightCfg& cfg = config.data.light;

        // Compensate for lamp self-illumination: when lamp is on, subtract offset
        int compensated = (int)ldr_reading;
        if (light_on)
            compensated -= (int)cfg.twilight_lamp_offset;
        if (compensated < 0) compensated = 0;

        bool reading_dark = (compensated < (int)cfg.twilight_threshold);

        if (reading_dark != is_dark)
        {
            // Reading disagrees with current state — advance debounce timer
            if (m_ldr_change_since_ms == 0)
                m_ldr_change_since_ms = now_ms;
            else if (now_ms - m_ldr_change_since_ms >= LDR_DEBOUNCE_MS)
            {
                is_dark = reading_dark;
                m_ldr_change_since_ms = 0;
                Log.info("[GH] LDR %s (compensated %d, threshold %d)",
                         is_dark ? "dark" : "bright", compensated, cfg.twilight_threshold);
            }
        }
        else
        {
            m_ldr_change_since_ms = 0;  // Reading agrees — reset timer
        }

        // Time schedule check
        bool in_schedule = false;
        if (g_gh_time_synced)
        {
            time_t now = time(nullptr);
            struct tm* tm_now = localtime(&now);
            for (int i = 0; i < cfg.num_time_spans && i < MAX_TIME_SPANS; i++)
            {
                if (!cfg.time_spans[i].enabled) continue;
                if (isTimeInSpan(tm_now, cfg.time_spans[i]))
                {
                    in_schedule = true;
                    break;
                }
            }
        }

        bool should_be_on = is_dark && in_schedule;
        if (should_be_on != light_on)
        {
            light_on = should_be_on;
            digitalWrite(LIGHT_PIN, should_be_on ? RELAY_ON : RELAY_OFF);
            Log.info("[GH] Light %s", should_be_on ? "ON" : "OFF");
        }
    }

    // --- Irrigation evaluation (CH2) ---
    // Dispatches to the correct evaluation strategy based on irr_mode.
    void evaluateIrrigation(unsigned long now_ms)
    {
        bool prev_valve = m_irr.valve_on;
        const IrrigationCfg&     irrCfg = config.data.irrigation;
        const IrrigationExtData& ext    = config.irr_ext;

        if (ext.irr_mode == IRR_MODE_SCHEDULE)
        {
            // Time-program driven — use NTP time
            struct tm* tm_now = nullptr;
            if (g_gh_time_synced)
            {
                time_t t = time(nullptr);
                tm_now = localtime(&t);
            }
            m_irr.evaluateSchedule(tm_now,
                                   ext.time_progs, ext.num_time_progs,
                                   irrCfg.enabled, m_tank_empty, m_user_aborted);
        }
        else
        {
            // Legacy moisture-sensor driven
            m_irr.evaluate(moisture_pct, moisture_sensor_ok, irrCfg, now_ms);
        }

        if (m_irr.valve_on != prev_valve)
        {
            digitalWrite(VALVE_PIN, m_irr.valve_on ? RELAY_ON : RELAY_OFF);
            onValveChanged(m_irr.valve_on);
        }
    }

    // Manage pump coupling when valve state changes.
    void onValveChanged(bool on)
    {
        if (on)
        {
            m_irr_pump_on = true;
            if (on_pump_request && !m_external_pump_on)
                on_pump_request(true);
        }
        else
        {
            m_irr_pump_on = false;
            if (on_pump_request && !m_external_pump_on)
                on_pump_request(false);
        }
    }

    // Close valve and transition irrigation to soak when pump is forced off.
    void pumpForcedOff(unsigned long now_ms)
    {
        if (m_irr.valve_on)
        {
            m_irr.forceOff(now_ms);
            digitalWrite(VALVE_PIN, RELAY_OFF);
            m_irr_pump_on = false;
            Log.info("[GH] Pump forced off — valve closed, entering soak");
        }
    }
};
