#pragma once

#include <EEPROM.h>
#include <time.h>
#include <cstdint>

#include "irrigation.h"

// Bump magic when struct layout changes so old EEPROM content is discarded.
#define CONFIG_MAGIC      0x4E4C4309  // "NLC" + version 9 (Nexa mDNS discovery)
#define MAX_TIME_SPANS    4
#define MAX_NEXA_PLUGS    4
#define NEXA_NAME_LEN     16
#define NEXA_HOST_LEN     32
#define EEPROM_SIZE       512

// --- Light channel (CH1) config ---
struct TimeSpanCfg {
    uint8_t start_hour;     // 0-23
    uint8_t start_minute;   // 0-59
    uint8_t end_hour;       // 0-23
    uint8_t end_minute;     // 0-59
    uint8_t weekdays;       // bitmask: bit0=Mon, bit1=Tue, ... bit6=Sun. 0x7F=all
    uint8_t enabled;        // 0=disabled, 1=enabled
    uint8_t _pad[2];        // alignment padding
};

struct LightCfg {
    uint16_t twilight_on;   // ADC reading below this → dark (turn on)
    uint16_t twilight_off;  // ADC reading above this → light (turn off)
    uint8_t  num_time_spans;
    uint8_t  _pad;
    TimeSpanCfg time_spans[MAX_TIME_SPANS];
};

// --- Nexa smart plug config ---
// Each plug is discovered via mDNS (_systemnexa2._tcp) and controlled via
// HTTP GET http://<resolved_ip>:3000/state?v=0|1
struct NexaPlugCfg {
    char     hostname[NEXA_HOST_LEN];       // mDNS hostname (e.g. "WPO-01-abc", no .local suffix)
    char     name[NEXA_NAME_LEN];           // Display name (null-terminated)
    uint8_t  enabled;
    uint8_t  num_time_spans;
    uint8_t  _pad[2];
    TimeSpanCfg time_spans[MAX_TIME_SPANS]; // Per-plug time schedules
};

struct NexaCfg {
    uint8_t  num_plugs;                     // 0..MAX_NEXA_PLUGS
    uint8_t  _pad[3];
    NexaPlugCfg plugs[MAX_NEXA_PLUGS];
};

struct GreenhouseCfgData {
    uint32_t magic;
    LightCfg      light;
    IrrigationCfg irrigation;
    NexaCfg       nexa;
};

// --- Shared time-in-span helper (used by greenhouse.h and nexa.h) ---
// Handles wrapping around midnight (e.g. 21:00 → 07:00).
inline bool isTimeInSpan(const struct tm* tm_now, const TimeSpanCfg& ts)
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

class GreenhouseConfig {
public:
    GreenhouseCfgData data;

    void begin()
    {
        EEPROM.begin(EEPROM_SIZE);
        load();
    }

    void load()
    {
        EEPROM.get(0, data);
        if (data.magic != CONFIG_MAGIC)
        {
            setDefaults();
            save();
        }
    }

    void save()
    {
        data.magic = CONFIG_MAGIC;
        EEPROM.put(0, data);
        EEPROM.commit();
    }

    void setDefaults()
    {
        memset(&data, 0, sizeof(data));
        data.magic = CONFIG_MAGIC;

        // --- Light defaults ---
        data.light.twilight_on   = 300;    // Below 300 = dark
        data.light.twilight_off  = 400;    // Above 400 = light
        data.light.num_time_spans = 1;
        data.light.time_spans[0].start_hour   = 21;
        data.light.time_spans[0].start_minute  = 0;
        data.light.time_spans[0].end_hour      = 7;
        data.light.time_spans[0].end_minute    = 0;
        data.light.time_spans[0].weekdays      = 0x7F; // All days
        data.light.time_spans[0].enabled       = 1;

        // --- Irrigation defaults ---
        // Capacitive soil moisture sensor with amplifier on GPIO 9 (ADC1), 1MΩ pulldown.
        //   Wet cal is fixed constant MOISTURE_CAL_WET = 100 ADC (in firmware).
        //   ~2.5 V dry air → ~775 ADC = 0%  (calibrate with the Dry button)
        // Gardena micro-drip in greenhouse: short bursts + soak.
        data.irrigation.moisture_cal_dry   = 775;   // ~2.5 V dry air   →   0%
        data.irrigation.dry_threshold_pct  = 30;    // Start irrigating below 30%
        data.irrigation.wet_threshold_pct  = 60;    // Stop  irrigating above 60%
        data.irrigation.irrigate_on_min    = 3;     // 3 min watering
        data.irrigation.irrigate_off_min   = 20;    // 20 min soak
        data.irrigation.max_cycles         = 6;     // Safety: max 6 cycles
        data.irrigation.enabled            = 0;     // Off by default

        // --- Nexa defaults (all empty/disabled) ---
        data.nexa.num_plugs = 0;
    }
};
