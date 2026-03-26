#pragma once

#ifdef FEATURE_GREENHOUSE

#include <EEPROM.h>

// Bump magic when struct layout changes so old EEPROM content is discarded.
#define CONFIG_MAGIC      0x4E4C4307  // "NLC" + version 7
#define MAX_TIME_SPANS    4
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

// --- Irrigation channel (CH2) config ---
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
};

struct GreenhouseCfgData {
    uint32_t magic;
    LightCfg      light;
    IrrigationCfg irrigation;
};

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
    }
};

#endif // FEATURE_GREENHOUSE
