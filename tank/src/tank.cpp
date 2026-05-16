#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <RingBufCPP.h>
#include <TimeLib.h>
#include <HardwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
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
static int filter_entries = 0;

// Health state
static bool sd_ok = false;
static bool sensor_ok = false;

static HardwareSerial distSerial(1);
static OneWire oneWire;
static DallasTemperature tempSensors(&oneWire);
static float g_temperatureC = 20.0f;  // default until first reading

// Read one distance from A02YYUW using triggered (on-demand) mode.
// Send 0x01 to request a single measurement; sensor replies with one
// 4-byte packet: 0xFF distH distL checksum. This avoids continuous
// streaming which fills the RX FIFO with stale data between readings.
// Returns distance in mm, or -1 on timeout/checksum error.
static int readUARTDistanceMM()
{
    // Discard any stale bytes accumulated since last reading
    while (distSerial.available())
        distSerial.read();

    // Send trigger byte to request one measurement
    distSerial.write((uint8_t)0x01);

    // Wait for the 4-byte response: 0xFF distH distL checksum
    // At 9600 baud, one byte ≈ 1 ms; sensor responds within ~100-300 ms.
    const uint32_t TIMEOUT_MS = 400;
    unsigned long start = millis();
    int badChecksum = 0;

    while (millis() - start < TIMEOUT_MS) {
        if (!distSerial.available()) continue;
        uint8_t b = distSerial.read();
        if (b != 0xFF) continue;
        uint8_t buf[3] = {0};
        uint8_t idx = 0;
        unsigned long t = millis();
        while (idx < 3 && millis() - t < 50) {
            if (distSerial.available())
                buf[idx++] = distSerial.read();
        }
        if (idx < 3) continue;
        uint8_t checksum = (uint8_t)(0xFF + buf[0] + buf[1]);
        if (checksum != buf[2]) {
            badChecksum++;
            continue;
        }
        int dist = (int)((buf[0] << 8) | buf[1]);
        //Log.info("UART raw: %02X %02X %02X %02X -> %d mm", 0xFF, buf[0], buf[1], buf[2], dist);
        return dist;
    }
    Log.warn("UART: no valid packet after trigger (badCS=%d)", badChecksum);
    return -1;
}

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
    if (!sd_ok)
        return;
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
    if (!sd_ok)
        return;
    File file = SD.open(path, SD_APPEND);
    if (file)
    {
        String sampStr = sample_to_json(sample);
        if (file.size() > 0)
        {
            sampStr = ",\n" + sampStr;
        }
        file.print(sampStr);
        file.close();
        sd_ok = true;
    }
    else
    {
        Log.error("SD: failed to open %s for writing", path.c_str());
        sd_ok = false;
    }
}

static bool take_sample()
{
    MedianFilter<long> fastMedianFilter(FAST_MEDIAN_FILTER_LEN);
    int valid_samples = 0;

    for (int i = 0; i < FAST_MEDIAN_FILTER_LEN; i++)
    {
        int dist_mm = readUARTDistanceMM();
        if (dist_mm <= 0)
        {
            Log.warn("Distance sensor timeout (sample %d/%d)", i + 1, FAST_MEDIAN_FILTER_LEN);
            break;
        }
        fastMedianFilter.AddValue(dist_mm);
        valid_samples++;
    }

    if (valid_samples == 0)
    {
        // All readings failed — don't corrupt the mean filter with zeros
        Log.error("Distance sensor: no valid readings");
        sensor_ok = false;
        return filter_entries >= SLOW_MEAN_FILTER_LEN;
    }
    sensor_ok = true;

    // A02YYUW reports mm assuming ~343 m/s (≈20 °C).  Compensate using measured
    // air temperature: v_actual = 331.3 + 0.606 * T  (m/s)
    float v_actual = 331.3f + 0.606f * g_temperatureC;
    long compensated_mm = (long)(fastMedianFilter.GetFiltered() * v_actual / 343.0f);
    meanFilter.AddValue(compensated_mm);

    if (filter_entries < SLOW_MEAN_FILTER_LEN)
    {
        filter_entries++;
        if (filter_entries == SLOW_MEAN_FILTER_LEN)
        {
            Log.info("Distance filter filled");
        }
    }
    return filter_entries >= SLOW_MEAN_FILTER_LEN;
}

void tank_init()
{
    // A02YYUW UART — triggered mode: TX sends 0x01 to request each measurement.
    Log.info("UART dist sensor init: RX=GPIO%d TX=GPIO%d baud=9600 (triggered mode)", DIST_UART_RX_PIN, DIST_UART_TX_PIN);
    distSerial.begin(9600, SERIAL_8N1, DIST_UART_RX_PIN, DIST_UART_TX_PIN);

    // DS18B20 temperature sensor
    Log.info("1-Wire temp sensor init: GPIO%d", ONE_WIRE_PIN);

    // Verify external pullup: configure as input (no internal pull), read the idle level.
    // With a working external pullup, the idle state should be HIGH.
    pinMode(ONE_WIRE_PIN, INPUT);
    int idleLevel = digitalRead(ONE_WIRE_PIN);
    Log.info("1-Wire bus idle level (INPUT, no pull): %s", idleLevel ? "HIGH (pullup ok)" : "LOW (no pullup or bus stuck!)");

    // Try driving low briefly, release, read back — verifies the pin can go low and returns high
    pinMode(ONE_WIRE_PIN, OUTPUT);
    digitalWrite(ONE_WIRE_PIN, LOW);
    delayMicroseconds(10);
    pinMode(ONE_WIRE_PIN, INPUT);
    delayMicroseconds(100); // give pullup time to charge
    int afterRelease = digitalRead(ONE_WIRE_PIN);
    Log.info("1-Wire bus after drive-low-release: %s", afterRelease ? "HIGH (ok)" : "LOW (stuck or no pullup)");

    oneWire.begin(ONE_WIRE_PIN);
    // Manual reset pulse — returns true if any device pulls the bus low (presence detect)
    bool presence = oneWire.reset();
    Log.info("1-Wire reset pulse: %s", presence ? "PRESENCE detected" : "NO presence (check wiring + 4.7k pullup)");
    tempSensors.begin();
    int devCount = tempSensors.getDeviceCount();
    Log.info("1-Wire devices found: %d", devCount);
    if (devCount == 0)
    {
        Log.error("No DS18B20 found on GPIO%d! Needs 4.7k pullup to 3.3V. Using default %.1f C", ONE_WIRE_PIN, g_temperatureC);
    }
    else
    {
        DeviceAddress addr;
        if (tempSensors.getAddress(addr, 0))
        {
            Log.info("DS18B20 addr: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                     addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);
        }
        tempSensors.setResolution(9); // 9-bit: ~94 ms conversion
        tempSensors.requestTemperatures();
        float t = tempSensors.getTempCByIndex(0);
        Log.info("DS18B20 raw reading: %.1f C (DISCONNECTED=%.1f)", t, (float)DEVICE_DISCONNECTED_C);
        if (t != DEVICE_DISCONNECTED_C)
        {
            g_temperatureC = t;
            Log.info("Initial air temperature: %.1f C", g_temperatureC);
        }
        else
        {
            Log.error("DS18B20 returned DISCONNECTED. Using default %.1f C", g_temperatureC);
        }
    }

    last_hour = hour();
    last_min = minute();

    // Try initial distance reading
    Log.info("Taking initial distance sample...");
    take_sample();

    if (sensor_ok)
    {
        Log.info("Initial distance: %ld mm", meanFilter.GetFiltered());
    }
    else
    {
        Log.error("Initial distance reading FAILED. Check UART wiring (RX/TX may be swapped).");
        Log.info("Checking if any bytes on UART...");
        delay(300); // Give sensor time to send a packet
        int avail = distSerial.available();
        Log.info("UART bytes available: %d", avail);
        if (avail == 0)
        {
            Log.error("No UART data. Possible issues: wrong RX pin, sensor not powered, or TX/RX swapped.");
        }
    }
}

uint16_t tank_get_level()
{
    if (filter_entries == 0) return 0;
    // Filter stores mm directly (A02YYUW outputs mm, temp-compensated)
    long distance_mm = meanFilter.GetFiltered();
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

    String json = "{\"LVL\":" + String(level) + ",\"HARV\":" + String(harvest) + ",\"CONS\":" + String(consumed);
    json += ",\"TEMP\":" + String(g_temperatureC, 1);
    json += "}";
    return json;
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

void tank_set_sd_ok(bool ok) { sd_ok = ok; }
bool tank_is_sd_ok() { return sd_ok; }
bool tank_is_sensor_ok() { return sensor_ok; }

String tank_get_health_json()
{
    return "{\"SD\":" + String(sd_ok ? 1 : 0) +
           ",\"SENSOR\":" + String(sensor_ok ? 1 : 0) + "}";
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
    static int last_second = -1;

    // Tick consumption every second so short pump runs (< 1 min) are detected
    int cur_second = second();
    if (last_second != cur_second)
    {
        last_second = cur_second;
        consumption_per_hour.tick();
        consumption_per_day.tick();
    }

    if (last_min == MINUTE())
    {
        return;
    }
    last_min = MINUTE();

    // Refresh air temperature before taking distance samples
    tempSensors.requestTemperatures();
    float t = tempSensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C)
    {
        g_temperatureC = t;
    }
    else
    {
        Log.warn("DS18B20 read failed (DISCONNECTED), using last known: %.1f C", g_temperatureC);
    }

    bool filter_filled = take_sample();
    if (filling && filter_filled)
    {
        filling = false;
        last_hour = HOUR();
    }

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
