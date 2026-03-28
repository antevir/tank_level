#pragma once

#ifdef FEATURE_GREENHOUSE

#include <Arduino.h>
#include <time.h>
#ifdef ESP32
# include <esp_sntp.h>
#else
# include <coredecls.h>
#endif

#include "greenhouse_config.h"
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

// --- Irrigation state machine ---
enum IrrigationState {
    IRR_IDLE,       // Waiting for soil to get dry
    IRR_WATERING,   // Valve is open
    IRR_SOAKING,    // Valve closed, waiting for water to percolate
    IRR_PAUSED      // Safety limit reached — paused until soil is wet again
};

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

    // --- Irrigation state ---
    bool             valve_on  = false;
    IrrigationState  irr_state = IRR_IDLE;
    uint8_t          irr_cycle_count = 0;       // Current cycle in this burst
    unsigned long    irr_state_start_ms = 0;    // When current state began

    bool isTimeSynced() const { return g_gh_time_synced; }

    // Called from main.cpp when the LOCAL user presses the pump button.
    // pump_on: true = user wants pump ON, false = user wants pump OFF.
    void userPumpToggle(bool pump_on)
    {
        m_external_pump_on = pump_on;
        if (on_pump_request)
            on_pump_request(pump_on);

        if (!pump_on)
            pumpForcedOff();
    }

    // Called from pump_state_cb whenever the tank server reports a new state.
    // Detects external pump activation (another remote_btn or tank button)
    // and external pump shutoff (timeout, dry-run, other user).
    void notifyPumpState(PumpState state)
    {
        bool pump_on = (state >= PumpIdle);  // Idle, Running, Warning = pump enabled
        if (pump_on && !m_irr_pump_on)
        {
            // Pump turned on but not by our irrigation → someone else did it
            m_external_pump_on = true;
        }
        else if (!pump_on)
        {
            // Pump turned off (Off, DryRun) — clear overrides
            m_external_pump_on = false;
            m_irr_pump_on = false;
            pumpForcedOff();
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

    // LDR debounce: track when reading first crossed each twilight threshold
    unsigned long  m_ldr_dark_since_ms   = 0;   // When LDR first dropped below twilight_on
    unsigned long  m_ldr_bright_since_ms = 0;   // When LDR first rose above twilight_off

    // --- Light evaluation (CH1): digital on/off ---
    void evaluateLight(unsigned long now_ms)
    {
        const LightCfg& cfg = config.data.light;
        // LDR must stay above/below threshold for this long before state changes
        const unsigned long LDR_DEBOUNCE_MS = 5UL * 60UL * 1000UL;

        // Twilight detection with hysteresis and 5-minute debounce
        if (is_dark)
        {
            if (ldr_reading > cfg.twilight_off)
            {
                if (m_ldr_bright_since_ms == 0)
                    m_ldr_bright_since_ms = now_ms;
                else if (now_ms - m_ldr_bright_since_ms >= LDR_DEBOUNCE_MS)
                {
                    is_dark = false;
                    m_ldr_bright_since_ms = 0;
                    Log.info("[GH] LDR bright for 5 min, is_dark = false");
                }
            }
            else
            {
                m_ldr_bright_since_ms = 0;  // Dropped back — reset timer
            }
        }
        else
        {
            if (ldr_reading < cfg.twilight_on)
            {
                if (m_ldr_dark_since_ms == 0)
                    m_ldr_dark_since_ms = now_ms;
                else if (now_ms - m_ldr_dark_since_ms >= LDR_DEBOUNCE_MS)
                {
                    is_dark = true;
                    m_ldr_dark_since_ms = 0;
                    Log.info("[GH] LDR dark for 5 min, is_dark = true");
                }
            }
            else
            {
                m_ldr_dark_since_ms = 0;  // Recovered — reset timer
            }
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

    // --- Irrigation evaluation (CH2): cycle-based ---
    //
    // Gardena micro-drip best practice for greenhouse use:
    //   1) When soil moisture % < dry_threshold_pct → soil is too dry
    //   2) Open valve for irrigate_on_min (e.g. 3 min) — fully on, no interruption
    //   3) Close valve, wait irrigate_off_min (e.g. 20 min) for water
    //      to reach the sensor depth (7 cm) through the drip emitters
    //   4) Re-check moisture; if still dry, repeat up to max_cycles
    //   5) If max_cycles reached, pause until soil reads wet again
    //      (prevents flooding if sensor fails or is displaced)
    //
    void evaluateIrrigation(unsigned long now_ms)
    {
        const IrrigationCfg& cfg = config.data.irrigation;

        if (!cfg.enabled)
        {
            if (valve_on) irrigationSetValve(false);
            irr_state = IRR_IDLE;
            irr_cycle_count = 0;
            return;
        }

        // Sensor disconnected: close valve and stay idle until sensor returns
        if (!moisture_sensor_ok)
        {
            if (valve_on) irrigationSetValve(false);
            if (irr_state != IRR_IDLE)
            {
                Log.warn("[GH] Moisture sensor disconnected — aborting irrigation");
                irr_state = IRR_IDLE;
                irr_cycle_count = 0;
            }
            return;
        }

        unsigned long on_sec  = (unsigned long)cfg.irrigate_on_min  * 60UL;
        unsigned long off_sec = (unsigned long)cfg.irrigate_off_min * 60UL;
        unsigned long elapsed_sec = (now_ms - irr_state_start_ms) / 1000UL;

        switch (irr_state)
        {
        case IRR_IDLE:
            // Soil is dry? (low percent = dry) → start watering
            if (moisture_pct < cfg.dry_threshold_pct)
            {
                irr_cycle_count = 0;
                startWatering(now_ms);
            }
            break;

        case IRR_WATERING:
            // Keep valve fully ON for the entire on_sec duration.
            // Only check moisture after the full cycle to avoid noise-induced
            // premature cutoff (relay must not chatter).
            if (elapsed_sec >= on_sec)
            {
                irrigationSetValve(false);
                irr_state = IRR_SOAKING;
                irr_state_start_ms = now_ms;
                Log.info("[GH] Irrigation: soaking (%lu min)", cfg.irrigate_off_min);
            }
            break;

        case IRR_SOAKING:
            if (elapsed_sec >= off_sec)
            {
                // Soak done — check if soil is wet enough
                if (moisture_pct >= cfg.wet_threshold_pct)
                {
                    irr_state = IRR_IDLE;
                    irr_cycle_count = 0;
                    Log.info("[GH] Irrigation: soil now wet (%d%%), done", moisture_pct);
                }
                else if (irr_cycle_count >= cfg.max_cycles)
                {
                    irr_state = IRR_PAUSED;
                    Log.warn("[GH] Irrigation: max cycles (%d) reached, pausing", cfg.max_cycles);
                }
                else
                {
                    // Still dry → another cycle
                    startWatering(now_ms);
                }
            }
            break;

        case IRR_PAUSED:
            // Stay paused until sensor reads wet
            if (moisture_pct >= cfg.wet_threshold_pct)
            {
                irr_state = IRR_IDLE;
                irr_cycle_count = 0;
                Log.info("[GH] Irrigation: soil wet again, unpaused");
            }
            break;
        }
    }

    void startWatering(unsigned long now_ms)
    {
        irr_cycle_count++;
        irrigationSetValve(true);
        irr_state = IRR_WATERING;
        irr_state_start_ms = now_ms;
        Log.info("[GH] Irrigation: cycle %d, watering (%lu min)",
                 irr_cycle_count, config.data.irrigation.irrigate_on_min);
    }

    void setValve(bool on)
    {
        valve_on = on;
        digitalWrite(VALVE_PIN, on ? RELAY_ON : RELAY_OFF);
    }

    // Valve control with coupled pump management for irrigation.
    // Starts pump when valve opens (unless already running externally).
    // Stops pump when valve closes (unless started by a user).
    void irrigationSetValve(bool on)
    {
        setValve(on);
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

    // Close valve and transition irrigation to soak when pump is forced off
    // (by user button press or server shutoff).
    void pumpForcedOff()
    {
        if (valve_on)
        {
            setValve(false);
            m_irr_pump_on = false;
            if (irr_state == IRR_WATERING)
            {
                irr_state = IRR_SOAKING;
                irr_state_start_ms = millis();
                Log.info("[GH] Pump forced off — valve closed, entering soak");
            }
        }
    }

    // Convert raw ADC to moisture percent.
    // cal_wet = ADC in water (100%), cal_dry = ADC in dry air (0%).
    // Higher ADC = drier, so we invert.
    static uint8_t adcToPercent(uint16_t adc, uint16_t cal_wet, uint16_t cal_dry)
    {
        if (cal_dry <= cal_wet) return 50;  // miscalibrated — return midpoint
        int range = (int)cal_dry - (int)cal_wet;
        int pct   = 100 - ((int)adc - (int)cal_wet) * 100 / range;
        return (uint8_t)constrain(pct, 0, 100);
    }

    static bool isTimeInSpan(const struct tm* tm_now, const TimeSpanCfg& ts)
    {
        // tm_wday: 0=Sun, 1=Mon ... 6=Sat → bitmask: bit0=Mon ... bit6=Sun
        int bit;
        if (tm_now->tm_wday == 0)
            bit = 6;
        else
            bit = tm_now->tm_wday - 1;

        if (!((ts.weekdays >> bit) & 1))
            return false;

        int now_min   = tm_now->tm_hour * 60 + tm_now->tm_min;
        int start_min = ts.start_hour   * 60 + ts.start_minute;
        int end_min   = ts.end_hour     * 60 + ts.end_minute;

        if (start_min <= end_min)
            return now_min >= start_min && now_min < end_min;
        else
            return now_min >= start_min || now_min < end_min;
    }
};

#endif // FEATURE_GREENHOUSE
