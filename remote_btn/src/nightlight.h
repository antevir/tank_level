#pragma once

#ifdef FEATURE_NIGHTLIGHT

#include <Arduino.h>
#include <time.h>
#include <coredecls.h>      // settimeofday_cb (ESP8266)

#include "nightlight_config.h"
#include "Log.h"

/*
 * Photo resistor circuit:
 *   - LDR: unknown type, ~60kΩ in darkness, ~2kΩ in ambient room light
 *   - Recommended: 10kΩ fixed resistor
 *   - Wiring: LDR from 3.3V to A0, 10kΩ from A0 to GND
 *   - Result: higher ADC reading = more light, lower = darker
 *   - Dark  (LDR≈60kΩ):  V = 3.3 × 10k/(10k+60k) ≈ 0.47V → ADC ≈ 146
 *   - Light (LDR≈2kΩ):   V = 3.3 × 10k/(10k+2k)  ≈ 2.75V → ADC ≈ 854
 *   - A 10kΩ resistor is near the geometric mean (√(2k×60k) ≈ 11k),
 *     giving good resolution in the twilight transition zone.
 */

// --- Pin definitions ---
#define LDR_PIN         A0
#define PWM_CH1_PIN     5       // D1
#define PWM_CH2_PIN     14      // D5

// --- Timing ---
#define HISTORY_SIZE        1440    // 24h at 1 sample per minute
#define HISTORY_INTERVAL_MS 60000   // 1 minute
#define SENSOR_AVG_MS       1000    // Average ADC readings every 1 second
#define PREVIEW_DURATION_MS 3000    // Preview PWM for 3 seconds

// Stockholm timezone
#define NL_TIME_ZONE "CET-1CEST,M3.5.0,M10.5.0/3"

static const uint8_t g_pwm_pins[NUM_CHANNELS] = { PWM_CH1_PIN, PWM_CH2_PIN };

// --- NTP sync callback (file-scope, singleton) ---
static volatile bool g_nl_time_synced = false;

static void nlTimeSyncCb()
{
    time_t now = time(nullptr);
    if (now > 1000000000UL)  // Sanity: after year ~2001
    {
        g_nl_time_synced = true;
    }
}

class Nightlight {
public:
    NightlightConfig config;

    // Current state (read by web server)
    uint16_t current_reading  = 0;
    uint16_t history[HISTORY_SIZE];
    uint16_t history_count     = 0;
    uint16_t history_write_idx = 0;
    bool     channel_on[NUM_CHANNELS]  = { false, false };
    bool     is_dark[NUM_CHANNELS]     = { false, false };

    bool isTimeSynced() const { return g_nl_time_synced; }

    void begin()
    {
        config.begin();

        // ESP8266 Arduino core defaults analogWrite to 8-bit range (0-255).
        // Our pwm_value and gammaCorrect() both operate on 0-1023.  Without
        // this call any value above 255 is clamped to 100% duty cycle.
        analogWriteRange(1023);

        pinMode(LDR_PIN, INPUT);
        for (int i = 0; i < NUM_CHANNELS; i++)
        {
            pinMode(g_pwm_pins[i], OUTPUT);
            analogWrite(g_pwm_pins[i], 0);
        }

        memset(history, 0, sizeof(history));

        // NTP setup (Stockholm timezone)
        settimeofday_cb(nlTimeSyncCb);
        configTime(NL_TIME_ZONE, "pool.ntp.org", "time.nist.gov");

        Log.info("[NL] Nightlight initialized");
    }

    // Call every tick (~100 ms) — accumulates ADC samples
    void sampleADC()
    {
        m_adc_sum += analogRead(LDR_PIN);
        m_adc_count++;
    }

    // Call every tick (~100 ms) — handles averaging, history, channel evaluation
    void update()
    {
        unsigned long now_ms = millis();

        // Average the accumulated ADC readings every SENSOR_AVG_MS
        if (now_ms - m_last_avg_ms >= SENSOR_AVG_MS)
        {
            m_last_avg_ms = now_ms;
            if (m_adc_count > 0)
            {
                current_reading = m_adc_sum / m_adc_count;
                m_adc_sum   = 0;
                m_adc_count = 0;
                m_sensor_ready = true;
            }
        }

        // Store history sample every HISTORY_INTERVAL_MS
        if (now_ms - m_last_history_ms >= HISTORY_INTERVAL_MS)
        {
            m_last_history_ms = now_ms;
            if (m_sensor_ready)
            {
                history[history_write_idx] = current_reading;
                history_write_idx = (history_write_idx + 1) % HISTORY_SIZE;
                if (history_count < HISTORY_SIZE)
                    history_count++;
            }
        }

        // Don't evaluate channels until sensor has valid data
        if (!m_sensor_ready) return;

        // Evaluate each channel
        for (int ch = 0; ch < NUM_CHANNELS; ch++)
        {
            evaluateChannel(ch, now_ms);
        }
    }

    // Start a timed preview (used by web UI)
    void startPreview(uint8_t ch, uint16_t pwm)
    {
        if (ch >= NUM_CHANNELS) return;
        m_preview_end_ms[ch] = millis() + PREVIEW_DURATION_MS;
        m_preview_pwm[ch]    = pwm;
        analogWrite(g_pwm_pins[ch], gammaCorrect(pwm));
        Log.info("[NL] Preview ch%d PWM=%d (hw=%d)", ch, pwm, gammaCorrect(pwm));
    }

    // Called after config is saved via web UI
    void onConfigSaved()
    {
        for (int ch = 0; ch < NUM_CHANNELS; ch++)
        {
            if (channel_on[ch])
            {
                // Currently on → apply new PWM immediately
                analogWrite(g_pwm_pins[ch], gammaCorrect(config.data.channels[ch].pwm_value));
            }
            else
            {
                // Not on → preview briefly so user can see the brightness
                startPreview(ch, config.data.channels[ch].pwm_value);
            }
        }
    }

private:
    uint32_t       m_adc_sum           = 0;
    uint16_t       m_adc_count         = 0;
    bool           m_sensor_ready      = false;
    unsigned long  m_last_avg_ms       = 0;
    unsigned long  m_last_history_ms   = 0;
    unsigned long  m_preview_end_ms[NUM_CHANNELS]  = { 0, 0 };
    uint16_t       m_preview_pwm[NUM_CHANNELS]     = { 0, 0 };

    void evaluateChannel(int ch, unsigned long now_ms)
    {
        // If preview is active, don't override
        if (m_preview_end_ms[ch] != 0)
        {
            if (now_ms < m_preview_end_ms[ch])
                return;  // Still in preview
            m_preview_end_ms[ch] = 0;  // Preview ended
        }

        const ChannelCfg& cfg = config.data.channels[ch];

        // --- Twilight detection with hysteresis ---
        if (is_dark[ch])
        {
            // Currently dark: go to "day" when reading rises above off-threshold
            if (current_reading > cfg.twilight_off)
                is_dark[ch] = false;
        }
        else
        {
            // Currently light: go to "night" when reading drops below on-threshold
            if (current_reading < cfg.twilight_on)
                is_dark[ch] = true;
        }

        // --- Time schedule check ---
        bool in_schedule = false;
        if (g_nl_time_synced)
        {
            time_t now = time(nullptr);
            struct tm* tm_now = localtime(&now);

            for (int i = 0; i < cfg.num_time_spans && i < MAX_TIME_SPANS; i++)
            {
                if (!cfg.time_spans[i].enabled)
                    continue;
                if (isTimeInSpan(tm_now, cfg.time_spans[i]))
                {
                    in_schedule = true;
                    break;
                }
            }
        }

        // --- Apply decision ---
        bool should_be_on = is_dark[ch] && in_schedule;
        if (should_be_on != channel_on[ch])
        {
            channel_on[ch] = should_be_on;
            if (should_be_on)
            {
                analogWrite(g_pwm_pins[ch], gammaCorrect(cfg.pwm_value));
                Log.info("[NL] Ch%d ON (PWM=%d, hw=%d)", ch, cfg.pwm_value, gammaCorrect(cfg.pwm_value));
            }
            else
            {
                analogWrite(g_pwm_pins[ch], 0);
                Log.info("[NL] Ch%d OFF", ch);
            }
        }
    }

    // Convert perceptual brightness (0-1023) to a linear PWM duty cycle (0-1023).
    // The human eye follows an approximate power law (gamma ≈ 2.2): a lamp at
    // 25% linear duty cycle already looks nearly full-brightness, and virtually
    // all visible dimming is crammed into the bottom few percent.  Applying gamma
    // correction inverts this: pwm_value=512 (50%) now actually looks like 50%.
    static uint16_t gammaCorrect(uint16_t perceived)
    {
        if (perceived == 0)      return 0;
        if (perceived >= 1023)   return 1023;
        float normalized = perceived / 1023.0f;
        float linear     = powf(normalized, 2.2f);
        return static_cast<uint16_t>(linear * 1023.0f + 0.5f);
    }

    static bool isTimeInSpan(const struct tm* tm_now, const TimeSpanCfg& ts)
    {
        // --- Weekday check ---
        // tm_wday: 0=Sunday, 1=Monday, ..., 6=Saturday
        // weekdays bitmask: bit0=Mon, bit1=Tue, ..., bit6=Sun
        int bit;
        if (tm_now->tm_wday == 0)
            bit = 6;                   // Sunday → bit 6
        else
            bit = tm_now->tm_wday - 1; // Mon=0 .. Sat=5

        if (!((ts.weekdays >> bit) & 1))
            return false;

        // --- Time range check (supports midnight crossing) ---
        int now_min   = tm_now->tm_hour * 60 + tm_now->tm_min;
        int start_min = ts.start_hour   * 60 + ts.start_minute;
        int end_min   = ts.end_hour     * 60 + ts.end_minute;

        if (start_min <= end_min)
        {
            // Same-day span (e.g. 08:00–20:00)
            return now_min >= start_min && now_min < end_min;
        }
        else
        {
            // Overnight span (e.g. 22:00–06:00)
            return now_min >= start_min || now_min < end_min;
        }
    }
};

#endif // FEATURE_NIGHTLIGHT
