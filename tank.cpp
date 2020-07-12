#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <RingBufCPP.h>
#include <TimeLib.h>
#include "MedianFilterLib.h"
#include "MeanFilterLib.h"

#include "Log.h"
#include "pins.h"
#include "tank.h"
#include "consumption.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define SLOW_MEAN_FILTER_LEN 8
#define FAST_MEDIAN_FILTER_LEN 5

#define TANK_HEIGHT_MM 920
#define TANK_TOP_DISTANCE_MM 40

#define MINUTE minute
#define HOUR hour

typedef struct
{
    uint16_t tank_level;
    unsigned long time_stamp;
    int consumption;
} sample_t;

static Consumption consumption_per_day;
static Consumption consumption_per_hour;
static int last_hour;
static int last_min;
static RingBufCPP<sample_t, 24> last24hSamples;
static int last_30days_offset = -1;

static MeanFilter<long> meanFilter(SLOW_MEAN_FILTER_LEN);

static String sample_to_json(sample_t &sample)
{
    return "{\"LVL\":" + String(sample.tank_level) + ",\"TS\":" + String(sample.time_stamp) + ",\"CONS\":" + String(sample.consumption) + "}";
}

static String year_file_path()
{
    char filename[32];
    sprintf(filename, "/%04d.json", year());
    return String(filename);
}

static String month_file_path()
{
    char filename[32];
    sprintf(filename, "/%04d-%02d.json", year(), month());
    return String(filename);
}

static bool get_next_line(File file)
{
    while (file.available())
    {
        if (file.read() == '\n')
        {
            return true;
        }
    }
    return false;
}

static int get_line_count(File file)
{
    int line_count = 0;
    file.seek(0);
    while (get_next_line(file))
    {
        line_count++;
    }
    return line_count;
}

static int get_line_offset(File file, int line_number)
{
    int line_count = 0;
    if (line_number == 0)
    {
        return 0;
    }
    file.seek(0);
    while (get_next_line(file))
    {
        if (++line_count == line_number)
        {
            return file.position();
        }
    }
    return -1;
}

static void update_last_30days()
{
    Log.info("Updating last 30 days");
    String path = year_file_path();
    if (!SD.exists(path))
    {
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (file)
    {
        int line_count = get_line_count(file);
        int line = (line_count > 30) ? line_count - 30 : 0;
        last_30days_offset = get_line_offset(file, line);
        file.close();
    }
    else
    {
        Log.error("Could not open year file");
    }
    Log.info("Done");
}

static void store_sample(String path, sample_t &sample)
{
    File file = SD.open(path, FILE_WRITE);
    if (file)
    {
        String sampStr = sample_to_json(sample);
        if (file.size() > 0)
        {
            sampStr = ",\n" + sampStr;
        }
        file.write(sampStr.c_str());
        file.close();
    }
    else
    {
        Log.error("Failed to open: %s", path.c_str());
    }
}

static bool take_sample()
{
    static int filter_entries = 0;
    MedianFilter<long> fastMedianFilter(FAST_MEDIAN_FILTER_LEN);
    for (int i = 0; i < FAST_MEDIAN_FILTER_LEN; i++)
    {
        digitalWrite(DIST_TRIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(DIST_TRIG_PIN, LOW);
        long duration = pulseIn(DIST_ECHO_PIN, HIGH);
        fastMedianFilter.AddValue(duration);
        if (duration == 0)
        {
            // Unable to get sensor value
            Log.warn("Unable to get distance value");
            break;
        }
    }
    meanFilter.AddValue(fastMedianFilter.GetFiltered());
    if (filter_entries < SLOW_MEAN_FILTER_LEN)
    {
        filter_entries++;
    }
    if (filter_entries == SLOW_MEAN_FILTER_LEN)
    {
        Log.info("Take sample (filled)");
        return true;
    }
    Log.info("Take sample (filling)");
    return false;
}

void tank_init()
{
    digitalWrite(DIST_TRIG_PIN, LOW);
    last_hour = hour();
    last_min = minute();

    // Take one sample to avoid div by zero in mean filter
    take_sample();
}

uint16 tank_get_level()
{
    long distance_mm = (meanFilter.GetFiltered() * 34) / 200; // org: (0.034 / 2)
    distance_mm -= TANK_TOP_DISTANCE_MM;
    if (distance_mm < 0)
        distance_mm = 0;
    long permille = ((TANK_HEIGHT_MM - MIN(distance_mm, TANK_HEIGHT_MM)) * 1000) / TANK_HEIGHT_MM;
    return permille;
}

String tank_get_stats_json()
{
    int diff = 0;
    uint16_t level = tank_get_level();
    if (!last24hSamples.isEmpty())
    {
        sample_t *old_sample = last24hSamples.peek(0);
        diff = level - old_sample->tank_level;
    }
    int consumed = consumption_per_day.get_consumption(false);
    int harvest = diff + consumed;

    return "{\"LVL\":" + String(level) + ",\"HARV\":" + String(harvest) + ",\"CONS\":" + String(consumed) + "}";
}

String tank_get_last_24h_json()
{
    int i = 0;
    String json;
    sample_t *sample = last24hSamples.peek(i);
    while (sample)
    {
        json += sample_to_json(*sample);
        sample = last24hSamples.peek(++i);
        if (sample)
        {
            json += ",";
        }
    }
    return "[" + json + "]";
}

bool tank_get_last_30days_file_and_offset(String &filename, int &data_offset)
{
    if (last_30days_offset == -1)
    {
        return false;
    }
    filename = year_file_path();
    data_offset = last_30days_offset;
    return true;
}

void tank_handle()
{
    static bool filling = true;

    if (last_min == MINUTE())
    {
        return;
    }
    last_min = MINUTE();

    bool filter_filled = take_sample();
    if (filling && filter_filled)
    {
        filling = false;
        last_hour = HOUR();
    }

    // Update consumption states each min
    consumption_per_hour.tick();
    consumption_per_day.tick();

    if (year() < 2000)
    {
        // The NTP client has failed
        // Since we don't know the real time we do no more...
        return;
    }

    // Now when we got the NTP time we need to update 30 day history context once
    static bool last_30days_updated = false;
    if (!last_30days_updated)
    {
        update_last_30days();
        last_30days_updated = true;
    }

    if (last_hour != HOUR() && !filling)
    {
        last_hour = HOUR();
        uint16_t level = tank_get_level();
        sample_t sample = {
            .tank_level = level,
            .time_stamp = (unsigned long)now()};

        Log.info("Sample, lvl: %d", sample.tank_level);

        if (last24hSamples.isFull())
        {
            sample_t dummy;
            last24hSamples.pull(&dummy);
        }
        sample.consumption = consumption_per_hour.get_consumption();
        last24hSamples.add(sample);

        if (last_hour == 23)
        {
            Log.info("Writing sample to SD card");
            sample.consumption = consumption_per_day.get_consumption();
            store_sample(year_file_path(), sample);
            store_sample(month_file_path(), sample);
            update_last_30days();
        }
    }
}
