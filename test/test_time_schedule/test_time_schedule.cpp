// Tests for isTimeInSpan() and time schedule logic
#include <unity.h>
#include <cstring>
#include <ctime>

// Pull in the greenhouse config to get struct definitions and isTimeInSpan()
#define ESP32
#include "greenhouse_config.h"

static struct tm make_tm(int wday, int hour, int min)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_wday = wday;  // 0=Sun, 1=Mon, ... 6=Sat
    t.tm_hour = hour;
    t.tm_min  = min;
    return t;
}

void setUp(void) {}
void tearDown(void) {}

static TimeSpanCfg make_span(int sh, int sm, int eh, int em, uint8_t weekdays, uint8_t enabled)
{
    TimeSpanCfg ts;
    memset(&ts, 0, sizeof(ts));
    ts.start_hour   = sh;
    ts.start_minute = sm;
    ts.end_hour     = eh;
    ts.end_minute   = em;
    ts.weekdays     = weekdays;
    ts.enabled      = enabled;
    return ts;
}

// --- Basic time range (no midnight wrap) ---

void test_time_in_span_inside(void)
{
    // 21:00–07:00 not wrapping case: 08:00–18:00 all days
    TimeSpanCfg ts = make_span(8, 0, 18, 0, 0x7F, 1);
    struct tm tm = make_tm(1, 12, 0);  // Monday 12:00
    TEST_ASSERT_TRUE(isTimeInSpan(&tm, ts));
}

void test_time_in_span_outside(void)
{
    TimeSpanCfg ts = make_span(8, 0, 18, 0, 0x7F, 1);
    struct tm tm = make_tm(1, 19, 0);  // Monday 19:00
    TEST_ASSERT_FALSE(isTimeInSpan(&tm, ts));
}

void test_time_in_span_at_start(void)
{
    TimeSpanCfg ts = make_span(8, 0, 18, 0, 0x7F, 1);
    struct tm tm = make_tm(2, 8, 0);   // Tuesday exactly 08:00
    TEST_ASSERT_TRUE(isTimeInSpan(&tm, ts));
}

void test_time_in_span_at_end(void)
{
    TimeSpanCfg ts = make_span(8, 0, 18, 0, 0x7F, 1);
    struct tm tm = make_tm(2, 18, 0);  // Tuesday exactly at end boundary (exclusive)
    TEST_ASSERT_FALSE(isTimeInSpan(&tm, ts));
}

void test_time_in_span_just_before_end(void)
{
    TimeSpanCfg ts = make_span(8, 0, 18, 0, 0x7F, 1);
    struct tm tm = make_tm(2, 17, 59);  // Tuesday 17:59
    TEST_ASSERT_TRUE(isTimeInSpan(&tm, ts));
}

// --- Midnight wrapping (e.g. 21:00 → 07:00) ---

void test_time_wrap_evening(void)
{
    TimeSpanCfg ts = make_span(21, 0, 7, 0, 0x7F, 1);
    struct tm tm = make_tm(3, 22, 30);  // Wednesday 22:30
    TEST_ASSERT_TRUE(isTimeInSpan(&tm, ts));
}

void test_time_wrap_morning(void)
{
    TimeSpanCfg ts = make_span(21, 0, 7, 0, 0x7F, 1);
    struct tm tm = make_tm(3, 3, 0);    // Wednesday 03:00
    TEST_ASSERT_TRUE(isTimeInSpan(&tm, ts));
}

void test_time_wrap_outside_day(void)
{
    TimeSpanCfg ts = make_span(21, 0, 7, 0, 0x7F, 1);
    struct tm tm = make_tm(3, 12, 0);   // Wednesday noon
    TEST_ASSERT_FALSE(isTimeInSpan(&tm, ts));
}

void test_time_wrap_at_start(void)
{
    TimeSpanCfg ts = make_span(21, 0, 7, 0, 0x7F, 1);
    struct tm tm = make_tm(3, 21, 0);   // Exactly 21:00
    TEST_ASSERT_TRUE(isTimeInSpan(&tm, ts));
}

void test_time_wrap_at_end(void)
{
    TimeSpanCfg ts = make_span(21, 0, 7, 0, 0x7F, 1);
    struct tm tm = make_tm(3, 7, 0);    // Exactly 07:00 (exclusive end)
    TEST_ASSERT_FALSE(isTimeInSpan(&tm, ts));
}

// --- Weekday filtering ---

void test_weekday_monday_only(void)
{
    // bit0=Mon only
    TimeSpanCfg ts = make_span(8, 0, 18, 0, 0x01, 1);
    struct tm mon = make_tm(1, 12, 0);  // Monday (tm_wday=1)
    TEST_ASSERT_TRUE(isTimeInSpan(&mon, ts));

    struct tm tue = make_tm(2, 12, 0);  // Tuesday (tm_wday=2)
    TEST_ASSERT_FALSE(isTimeInSpan(&tue, ts));
}

void test_weekday_sunday(void)
{
    // bit6=Sun
    TimeSpanCfg ts = make_span(8, 0, 18, 0, 0x40, 1);
    struct tm sun = make_tm(0, 12, 0);  // Sunday (tm_wday=0)
    TEST_ASSERT_TRUE(isTimeInSpan(&sun, ts));

    struct tm sat = make_tm(6, 12, 0);  // Saturday (tm_wday=6)
    TEST_ASSERT_FALSE(isTimeInSpan(&sat, ts));
}

void test_weekday_weekend(void)
{
    // bit5=Sat + bit6=Sun
    TimeSpanCfg ts = make_span(8, 0, 18, 0, 0x60, 1);
    struct tm sat = make_tm(6, 12, 0);  // Saturday
    TEST_ASSERT_TRUE(isTimeInSpan(&sat, ts));
    struct tm sun = make_tm(0, 12, 0);  // Sunday
    TEST_ASSERT_TRUE(isTimeInSpan(&sun, ts));
    struct tm fri = make_tm(5, 12, 0);  // Friday
    TEST_ASSERT_FALSE(isTimeInSpan(&fri, ts));
}

void test_weekday_all_days(void)
{
    TimeSpanCfg ts = make_span(8, 0, 18, 0, 0x7F, 1);
    for (int d = 0; d <= 6; d++)
    {
        struct tm tm = make_tm(d, 12, 0);
        TEST_ASSERT_TRUE(isTimeInSpan(&tm, ts));
    }
}

// --- Edge cases ---

void test_span_midnight_exact(void)
{
    // 00:00–00:00 → same start/end, non-wrapping, zero-length window → always false
    TimeSpanCfg ts = make_span(0, 0, 0, 0, 0x7F, 1);
    struct tm tm = make_tm(1, 0, 0);
    TEST_ASSERT_FALSE(isTimeInSpan(&tm, ts));
}

void test_span_one_minute(void)
{
    TimeSpanCfg ts = make_span(12, 30, 12, 31, 0x7F, 1);
    struct tm inside = make_tm(1, 12, 30);
    TEST_ASSERT_TRUE(isTimeInSpan(&inside, ts));
    struct tm outside = make_tm(1, 12, 31);
    TEST_ASSERT_FALSE(isTimeInSpan(&outside, ts));
}

// --- Config defaults ---

void test_config_defaults(void)
{
    GreenhouseConfig cfg;
    cfg.setDefaults();

    TEST_ASSERT_EQUAL_UINT32(CONFIG_MAGIC, cfg.data.magic);

    // Light defaults
    TEST_ASSERT_EQUAL_UINT16(600, cfg.data.light.twilight_threshold);
    TEST_ASSERT_EQUAL_UINT16(200, cfg.data.light.twilight_lamp_offset);
    TEST_ASSERT_EQUAL_UINT8(1, cfg.data.light.num_time_spans);
    TEST_ASSERT_EQUAL_UINT8(16, cfg.data.light.time_spans[0].start_hour);
    TEST_ASSERT_EQUAL_UINT8(22, cfg.data.light.time_spans[0].end_hour);
    TEST_ASSERT_EQUAL_UINT8(0x7F, cfg.data.light.time_spans[0].weekdays);
    TEST_ASSERT_EQUAL_UINT8(1, cfg.data.light.time_spans[0].enabled);

    // Irrigation defaults
    TEST_ASSERT_EQUAL_UINT16(775, cfg.data.irrigation.moisture_cal_dry);
    TEST_ASSERT_EQUAL_UINT8(30, cfg.data.irrigation.dry_threshold_pct);
    TEST_ASSERT_EQUAL_UINT8(60, cfg.data.irrigation.wet_threshold_pct);
    TEST_ASSERT_EQUAL_UINT16(3, cfg.data.irrigation.irrigate_on_min);
    TEST_ASSERT_EQUAL_UINT16(20, cfg.data.irrigation.irrigate_off_min);
    TEST_ASSERT_EQUAL_UINT8(6, cfg.data.irrigation.max_cycles);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.data.irrigation.enabled);

    // Nexa defaults
    TEST_ASSERT_EQUAL_UINT8(0, cfg.data.nexa.num_plugs);
    TEST_ASSERT_EQUAL_UINT16(700, cfg.data.nexa.nexa_twilight_threshold);
}

void test_config_fits_in_eeprom(void)
{
    TEST_ASSERT_LESS_OR_EQUAL(EEPROM_SIZE, sizeof(GreenhouseCfgData));
}

int main(void)
{
    UNITY_BEGIN();

    // Time span tests
    RUN_TEST(test_time_in_span_inside);
    RUN_TEST(test_time_in_span_outside);
    RUN_TEST(test_time_in_span_at_start);
    RUN_TEST(test_time_in_span_at_end);
    RUN_TEST(test_time_in_span_just_before_end);

    // Midnight wrapping
    RUN_TEST(test_time_wrap_evening);
    RUN_TEST(test_time_wrap_morning);
    RUN_TEST(test_time_wrap_outside_day);
    RUN_TEST(test_time_wrap_at_start);
    RUN_TEST(test_time_wrap_at_end);

    // Weekday filtering
    RUN_TEST(test_weekday_monday_only);
    RUN_TEST(test_weekday_sunday);
    RUN_TEST(test_weekday_weekend);
    RUN_TEST(test_weekday_all_days);

    // Edge cases
    RUN_TEST(test_span_midnight_exact);
    RUN_TEST(test_span_one_minute);

    // Config
    RUN_TEST(test_config_defaults);
    RUN_TEST(test_config_fits_in_eeprom);

    return UNITY_END();
}
