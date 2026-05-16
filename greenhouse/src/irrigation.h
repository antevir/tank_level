#pragma once

#include <cstdint>
#include <time.h>

#define MAX_IRR_TIME_PROGS  4   // Maximum schedule-based time programs

// Irrigation control mode
#define IRR_MODE_MOISTURE   0   // Legacy: moisture-sensor driven
#define IRR_MODE_SCHEDULE   1   // Time-program driven (sensor hidden in UI)

// --- Irrigation state machine ---
enum IrrigationState {
    IRR_IDLE,       // Waiting for soil to get dry (or next schedule window)
    IRR_WATERING,   // Valve is open
    IRR_SOAKING,    // Valve closed, waiting for water to percolate
    IRR_PAUSED      // Safety limit reached — paused until soil is wet again
};

// --- Moisture ADC → percent conversion (pure function) ---
// cal_wet = ADC in water (100%), cal_dry = ADC in dry air (0%).
// Higher ADC = drier, so we invert.
inline uint8_t adcToPercent(uint16_t adc, uint16_t cal_wet, uint16_t cal_dry)
{
    if (cal_dry <= cal_wet) return 50;  // miscalibrated — return midpoint
    int range = (int)cal_dry - (int)cal_wet;
    int pct   = 100 - ((int)adc - (int)cal_wet) * 100 / range;
    if (pct < 0) return 0;
    if (pct > 100) return 100;
    return (uint8_t)pct;
}

// --- Schedule-based time program ---
// Each program runs every day: valve opens at start_hour:start_minute and runs for duration_min.
struct IrrigationTimeProg {
    uint8_t  start_hour;    // 0-23
    uint8_t  start_minute;  // 0-59
    uint16_t duration_min;  // Run duration in minutes (1-720)
    uint8_t  enabled;       // 0=disabled, 1=enabled
    uint8_t  _pad[3];       // alignment padding
};

// --- Irrigation channel (CH2) config (EEPROM layout — do not reorder) ---
// Wet calibration is fixed at MOISTURE_CAL_WET (100 ADC) — no need to calibrate the wet end
// because the sensor minimum (in water ~310 ADC) is already well above 0 and the clamping
// in adcToPercent() maps anything ≤ 100 ADC to 100%.
struct IrrigationCfg {
    uint16_t moisture_cal_dry;      // ADC value when sensor is in dry air (~775 = 2.5 V →   0%)
    uint8_t  dry_threshold_pct;     // % — start irrigating when moisture BELOW this
    uint8_t  wet_threshold_pct;     // % — stop  irrigating when moisture ABOVE this
    uint16_t irrigate_on_min;       // Minutes to run the valve per cycle
    uint16_t irrigate_off_min;      // Minutes to wait (soak) between cycles
    uint8_t  max_cycles;            // Safety limit: max consecutive cycles before forced pause
    uint8_t  enabled;               // 0=disabled, 1=enabled
    // NOTE: irr_mode and time_progs live in IrrigationExtData (separate EEPROM block)
};

// --- Irrigation state machine (pure logic, no hardware dependencies) ---
//
// Gardena micro-drip best practice for greenhouse use:
//   1) When soil moisture % < dry_threshold_pct → soil is too dry
//   2) Open valve for irrigate_on_min (e.g. 3 min)
//   3) Close valve, wait irrigate_off_min (e.g. 20 min) for water to soak
//   4) Re-check moisture; if still dry, repeat up to max_cycles
//   5) If max_cycles reached, pause until soil reads wet again
//
// Schedule mode (IRR_MODE_SCHEDULE):
//   Valve opens when any enabled time program is active (clock is in its window).
//   If tank is empty (PumpDryRun), irrigation is blocked and indicated in UI.
//
struct IrrigationCtrl {
    IrrigationState state      = IRR_IDLE;
    uint8_t         cycle_count = 0;
    unsigned long   state_start_ms = 0;
    bool            valve_on   = false;

    // Moisture-sensor mode: evaluate one tick of the state machine.
    // Returns true if valve_on changed.
    bool evaluate(uint8_t moisture_pct, bool sensor_ok,
                  const IrrigationCfg& cfg, unsigned long now_ms);

    // Schedule mode: evaluate one tick based on time programs.
    // tm_now:       current local time (null if NTP not synced — valve stays off).
    // progs:        pointer to the time-program array.
    // num_progs:    number of entries in progs.
    // enabled:      overall irrigation enabled flag.
    // tank_empty:   true = block irrigation, keep valve off.
    // user_aborted: in/out — set by caller when user manually stopped pump;
    //               cleared here automatically when the active window ends.
    // Returns true if valve_on changed.
    bool evaluateSchedule(const struct tm* tm_now,
                          const IrrigationTimeProg* progs, uint8_t num_progs,
                          bool enabled, bool tank_empty, bool& user_aborted);

    // External force-off (user button, server shutoff).
    // Closes valve and enters soak if was watering.
    void forceOff(unsigned long now_ms)
    {
        if (valve_on)
        {
            valve_on = false;
            if (state == IRR_WATERING)
            {
                state = IRR_SOAKING;
                state_start_ms = now_ms;
            }
        }
    }
};

// --- Implementation (inline, header-only) ---
inline bool IrrigationCtrl::evaluate(uint8_t moisture_pct, bool sensor_ok,
                                     const IrrigationCfg& cfg, unsigned long now_ms)
{
    bool prev_valve = valve_on;

    if (!cfg.enabled)
    {
        valve_on = false;
        state = IRR_IDLE;
        cycle_count = 0;
        return prev_valve != valve_on;
    }

    if (!sensor_ok)
    {
        valve_on = false;
        state = IRR_IDLE;
        cycle_count = 0;
        return prev_valve != valve_on;
    }

    unsigned long on_sec  = (unsigned long)cfg.irrigate_on_min  * 60UL;
    unsigned long off_sec = (unsigned long)cfg.irrigate_off_min * 60UL;
    unsigned long elapsed_sec = (now_ms - state_start_ms) / 1000UL;

    switch (state)
    {
    case IRR_IDLE:
        if (moisture_pct < cfg.dry_threshold_pct)
        {
            cycle_count = 1;
            valve_on = true;
            state = IRR_WATERING;
            state_start_ms = now_ms;
        }
        break;

    case IRR_WATERING:
        if (elapsed_sec >= on_sec)
        {
            valve_on = false;
            state = IRR_SOAKING;
            state_start_ms = now_ms;
        }
        break;

    case IRR_SOAKING:
        if (elapsed_sec >= off_sec)
        {
            if (moisture_pct >= cfg.wet_threshold_pct)
            {
                state = IRR_IDLE;
                cycle_count = 0;
            }
            else if (cycle_count >= cfg.max_cycles)
            {
                state = IRR_PAUSED;
            }
            else
            {
                cycle_count++;
                valve_on = true;
                state = IRR_WATERING;
                state_start_ms = now_ms;
            }
        }
        break;

    case IRR_PAUSED:
        if (moisture_pct >= cfg.wet_threshold_pct)
        {
            state = IRR_IDLE;
            cycle_count = 0;
        }
        break;
    }

    return prev_valve != valve_on;
}

// --- Schedule-based evaluation ---
inline bool IrrigationCtrl::evaluateSchedule(const struct tm* tm_now,
                                              const IrrigationTimeProg* progs, uint8_t num_progs,
                                              bool enabled, bool tank_empty, bool& user_aborted)
{
    bool prev_valve = valve_on;

    if (!enabled || tank_empty || tm_now == nullptr)
    {
        valve_on = false;
        state    = IRR_IDLE;
        return prev_valve != valve_on;
    }

    bool should_be_on = false;
    int  now_min = tm_now->tm_hour * 60 + tm_now->tm_min;

    for (int i = 0; i < num_progs && i < MAX_IRR_TIME_PROGS; i++)
    {
        const IrrigationTimeProg& tp = progs[i];
        if (!tp.enabled || tp.duration_min == 0) continue;

        int start_min = (int)tp.start_hour * 60 + (int)tp.start_minute;
        int end_min   = start_min + (int)tp.duration_min;

        if (end_min <= 1440)
        {
            // Does not wrap midnight
            if (now_min >= start_min && now_min < end_min)
            {
                should_be_on = true;
                break;
            }
        }
        else
        {
            // Wraps past midnight
            if (now_min >= start_min || now_min < (end_min - 1440))
            {
                should_be_on = true;
                break;
            }
        }
    }

    // When we exit the aborted window, the abort is done — ready for next window
    if (user_aborted && !should_be_on)
        user_aborted = false;

    valve_on = should_be_on && !user_aborted;
    state    = valve_on ? IRR_WATERING : IRR_IDLE;
    return prev_valve != valve_on;
}
