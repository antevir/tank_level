#pragma once

#ifdef FEATURE_NIGHTLIGHT

#include <EEPROM.h>

#define CONFIG_MAGIC      0x4E4C4303  // "NLC" + version
#define MAX_TIME_SPANS    4
#define NUM_CHANNELS      2
#define EEPROM_SIZE       512

struct TimeSpanCfg {
    uint8_t start_hour;     // 0-23
    uint8_t start_minute;   // 0-59
    uint8_t end_hour;       // 0-23
    uint8_t end_minute;     // 0-59
    uint8_t weekdays;       // bitmask: bit0=Mon, bit1=Tue, ... bit6=Sun. 0x7F=all
    uint8_t enabled;        // 0=disabled, 1=enabled
    uint8_t _pad[2];        // alignment padding
};

struct ChannelCfg {
    uint16_t pwm_value;     // 0-1023 (ESP8266 analogWrite range)
    uint16_t twilight_on;   // ADC reading below this → night detected (turn on)
    uint16_t twilight_off;  // ADC reading above this → day detected (turn off). Must be > twilight_on
    uint8_t  num_time_spans;
    uint8_t  _pad;
    TimeSpanCfg time_spans[MAX_TIME_SPANS];
};

struct NightlightCfgData {
    uint32_t magic;
    ChannelCfg channels[NUM_CHANNELS];
};

class NightlightConfig {
public:
    NightlightCfgData data;

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
        for (int i = 0; i < NUM_CHANNELS; i++)
        {
            data.channels[i].pwm_value     = 512;   // ~50%
            data.channels[i].twilight_on   = 300;    // Below this = night
            data.channels[i].twilight_off  = 400;    // Above this = day (hysteresis band: 300-400)
            data.channels[i].num_time_spans = 1;

            data.channels[i].time_spans[0].start_hour   = 21;
            data.channels[i].time_spans[0].start_minute  = 0;
            data.channels[i].time_spans[0].end_hour      = 7;
            data.channels[i].time_spans[0].end_minute    = 0;
            data.channels[i].time_spans[0].weekdays      = 0x7F; // All days
            data.channels[i].time_spans[0].enabled       = 1;
        }
    }
};

#endif // FEATURE_NIGHTLIGHT
