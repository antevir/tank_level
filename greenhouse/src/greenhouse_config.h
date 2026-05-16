#pragma once

#include <EEPROM.h>
#include <time.h>
#include <cstdint>

#include "irrigation.h"

// Bump magic when struct layout changes so old EEPROM content is discarded.
#define CONFIG_MAGIC      0x4E4C430A  // "NLC" + version 10 (single twilight, Nexa override)
#define MAX_TIME_SPANS    4
#define MAX_NEXA_PLUGS    4
#define NEXA_NAME_LEN     16
#define NEXA_HOST_LEN     32
#define EEPROM_SIZE       1024  // 0-511: main config, 512-1023: extended blocks

// Extended irrigation block — stored separately to preserve main config layout
#define IRR_EXT_MAGIC   0x49520001  // "IR" v1 (schedule time programs)
#define IRR_EXT_OFFSET  512

struct IrrigationExtData {
    uint32_t magic;
    uint8_t  irr_mode;          // IRR_MODE_MOISTURE or IRR_MODE_SCHEDULE
    uint8_t  num_time_progs;    // 0..MAX_IRR_TIME_PROGS
    uint8_t  _pad[2];
    IrrigationTimeProg time_progs[MAX_IRR_TIME_PROGS];
};

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
    uint16_t twilight_threshold;    // ADC below this = dark (single threshold for on/off)
    uint16_t twilight_lamp_offset;  // ADC offset to subtract when lamp is on (compensate LDR self-illumination)
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
    uint8_t  use_twilight;                  // 1 = require dark (LDR), 0 = schedule-only
    uint8_t  _pad;
    TimeSpanCfg time_spans[MAX_TIME_SPANS]; // Per-plug time schedules
};

struct NexaCfg {
    uint8_t  num_plugs;                     // 0..MAX_NEXA_PLUGS
    uint8_t  _pad;
    uint16_t nexa_twilight_threshold;       // Separate LDR threshold for indoor Nexa plugs (lower = darker)
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
    IrrigationExtData irr_ext;

    void begin()
    {
        EEPROM.begin(EEPROM_SIZE);
        load();
        loadIrrExt();
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

    void loadIrrExt()
    {
        EEPROM.get(IRR_EXT_OFFSET, irr_ext);
        if (irr_ext.magic != IRR_EXT_MAGIC)
        {
            setIrrExtDefaults();
            saveIrrExt();
        }
    }

    void saveIrrExt()
    {
        irr_ext.magic = IRR_EXT_MAGIC;
        EEPROM.put(IRR_EXT_OFFSET, irr_ext);
        EEPROM.commit();
    }

    void setDefaults()
    {
        memset(&data, 0, sizeof(data));
        data.magic = CONFIG_MAGIC;

        // --- Light defaults ---
        data.light.twilight_threshold   = 600;    // Below 600 = dark
        data.light.twilight_lamp_offset = 200;    // Lamp adds ~200 ADC to LDR
        data.light.num_time_spans = 1;
        data.light.time_spans[0].start_hour   = 16;
        data.light.time_spans[0].start_minute  = 0;
        data.light.time_spans[0].end_hour      = 22;
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
        data.nexa.nexa_twilight_threshold = 700;  // Indoor: darker threshold than greenhouse
    }

private:
    void setIrrExtDefaults()
    {
        memset(&irr_ext, 0, sizeof(irr_ext));
        irr_ext.magic          = IRR_EXT_MAGIC;
        irr_ext.irr_mode       = IRR_MODE_SCHEDULE;
        irr_ext.num_time_progs = 0;
    }
};
