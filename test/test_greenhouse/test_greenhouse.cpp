// Tests for pure greenhouse logic: adcToPercent and IrrigationCtrl
// No hardware mocks — these are free functions and plain structs.
#include <unity.h>
#include <cstring>
#include <ctime>

#include "greenhouse_config.h"

void setUp(void) {}
void tearDown(void) {}

// ─── adcToPercent tests ────────────────────────────────────────────────────

void test_adc_wet(void)
{
    TEST_ASSERT_EQUAL_UINT8(100, adcToPercent(100, 100, 775));
}

void test_adc_dry(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, adcToPercent(775, 100, 775));
}

void test_adc_mid(void)
{
    uint16_t mid = (100 + 775) / 2;  // 437
    uint8_t pct = adcToPercent(mid, 100, 775);
    TEST_ASSERT_INT_WITHIN(2, 50, pct);
}

void test_adc_clamp_above_wet(void)
{
    TEST_ASSERT_EQUAL_UINT8(100, adcToPercent(50, 100, 775));
}

void test_adc_clamp_below_dry(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, adcToPercent(900, 100, 775));
}

void test_adc_miscalibrated(void)
{
    TEST_ASSERT_EQUAL_UINT8(50, adcToPercent(500, 775, 100));
    TEST_ASSERT_EQUAL_UINT8(50, adcToPercent(500, 500, 500));
}

// ─── IrrigationCtrl tests ──────────────────────────────────────────────────

static IrrigationCfg make_cfg()
{
    IrrigationCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled            = 1;
    cfg.dry_threshold_pct  = 30;
    cfg.wet_threshold_pct  = 60;
    cfg.irrigate_on_min    = 3;    // 180 seconds
    cfg.irrigate_off_min   = 20;   // 1200 seconds
    cfg.max_cycles         = 3;
    cfg.moisture_cal_dry   = 775;
    return cfg;
}

void test_irr_stays_idle_when_wet(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();

    irr.evaluate(70, true, cfg, 1000);  // 70% > 30% threshold
    TEST_ASSERT_EQUAL(IRR_IDLE, irr.state);
    TEST_ASSERT_FALSE(irr.valve_on);
}

void test_irr_starts_when_dry(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();

    irr.evaluate(20, true, cfg, 1000);  // 20% < 30% threshold
    TEST_ASSERT_EQUAL(IRR_WATERING, irr.state);
    TEST_ASSERT_TRUE(irr.valve_on);
    TEST_ASSERT_EQUAL_UINT8(1, irr.cycle_count);
}

void test_irr_valve_changed_returns_true(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();

    bool changed = irr.evaluate(20, true, cfg, 1000);
    TEST_ASSERT_TRUE(changed);

    // Same state, no change
    changed = irr.evaluate(20, true, cfg, 2000);
    TEST_ASSERT_FALSE(changed);
}

void test_irr_soaks_after_on_time(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();

    irr.evaluate(20, true, cfg, 1000);            // Start watering at t=1000
    TEST_ASSERT_EQUAL(IRR_WATERING, irr.state);

    irr.evaluate(20, true, cfg, 1000 + 181000);   // 181s > 180s on_time
    TEST_ASSERT_EQUAL(IRR_SOAKING, irr.state);
    TEST_ASSERT_FALSE(irr.valve_on);
}

void test_irr_stops_when_wet_after_soak(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();
    unsigned long t = 1000;

    irr.evaluate(20, true, cfg, t);               // Start watering
    t += 181000;
    irr.evaluate(20, true, cfg, t);               // → soaking
    TEST_ASSERT_EQUAL(IRR_SOAKING, irr.state);

    t += 1201000;                                  // Soak done (>1200s)
    irr.evaluate(70, true, cfg, t);               // Soil now wet (70% > 60%)
    TEST_ASSERT_EQUAL(IRR_IDLE, irr.state);
    TEST_ASSERT_EQUAL_UINT8(0, irr.cycle_count);
}

void test_irr_cycles_when_still_dry(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();
    unsigned long t = 1000;

    irr.evaluate(20, true, cfg, t);    t += 181000;     // Water
    irr.evaluate(20, true, cfg, t);    t += 1201000;    // Soak
    irr.evaluate(20, true, cfg, t);                     // Still dry → cycle 2
    TEST_ASSERT_EQUAL(IRR_WATERING, irr.state);
    TEST_ASSERT_EQUAL_UINT8(2, irr.cycle_count);
    TEST_ASSERT_TRUE(irr.valve_on);
}

void test_irr_max_cycles_pauses(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();
    cfg.max_cycles = 2;
    unsigned long t = 1000;

    // Cycle 1: water → soak
    irr.evaluate(20, true, cfg, t);    t += 181000;
    irr.evaluate(20, true, cfg, t);    t += 1201000;
    // Cycle 2: still dry
    irr.evaluate(20, true, cfg, t);    t += 181000;
    irr.evaluate(20, true, cfg, t);    t += 1201000;
    // Still dry after max_cycles → paused
    irr.evaluate(20, true, cfg, t);
    TEST_ASSERT_EQUAL(IRR_PAUSED, irr.state);
}

void test_irr_unpauses_when_wet(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();
    cfg.max_cycles = 1;
    unsigned long t = 1000;

    // Cycle 1 → soak → still dry → paused
    irr.evaluate(20, true, cfg, t);    t += 181000;
    irr.evaluate(20, true, cfg, t);    t += 1201000;
    irr.evaluate(20, true, cfg, t);
    TEST_ASSERT_EQUAL(IRR_PAUSED, irr.state);

    // Soil becomes wet → back to idle
    irr.evaluate(70, true, cfg, t + 1000);
    TEST_ASSERT_EQUAL(IRR_IDLE, irr.state);
    TEST_ASSERT_EQUAL_UINT8(0, irr.cycle_count);
}

void test_irr_disabled_closes_valve(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();

    irr.evaluate(20, true, cfg, 1000);
    TEST_ASSERT_TRUE(irr.valve_on);

    cfg.enabled = 0;
    bool changed = irr.evaluate(20, true, cfg, 2000);
    TEST_ASSERT_FALSE(irr.valve_on);
    TEST_ASSERT_EQUAL(IRR_IDLE, irr.state);
    TEST_ASSERT_TRUE(changed);
}

void test_irr_sensor_disconnect_aborts(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();

    irr.evaluate(20, true, cfg, 1000);
    TEST_ASSERT_EQUAL(IRR_WATERING, irr.state);

    bool changed = irr.evaluate(0, false, cfg, 2000);  // sensor_ok = false
    TEST_ASSERT_EQUAL(IRR_IDLE, irr.state);
    TEST_ASSERT_FALSE(irr.valve_on);
    TEST_ASSERT_TRUE(changed);
}

void test_irr_force_off_during_watering(void)
{
    IrrigationCtrl irr;
    IrrigationCfg cfg = make_cfg();

    irr.evaluate(20, true, cfg, 1000);
    TEST_ASSERT_EQUAL(IRR_WATERING, irr.state);

    irr.forceOff(2000);
    TEST_ASSERT_FALSE(irr.valve_on);
    TEST_ASSERT_EQUAL(IRR_SOAKING, irr.state);
    TEST_ASSERT_EQUAL(2000UL, irr.state_start_ms);
}

void test_irr_force_off_during_idle_noop(void)
{
    IrrigationCtrl irr;
    irr.forceOff(1000);
    TEST_ASSERT_EQUAL(IRR_IDLE, irr.state);
    TEST_ASSERT_FALSE(irr.valve_on);
}

int main(void)
{
    UNITY_BEGIN();

    // ADC conversion
    RUN_TEST(test_adc_wet);
    RUN_TEST(test_adc_dry);
    RUN_TEST(test_adc_mid);
    RUN_TEST(test_adc_clamp_above_wet);
    RUN_TEST(test_adc_clamp_below_dry);
    RUN_TEST(test_adc_miscalibrated);

    // Irrigation state machine
    RUN_TEST(test_irr_stays_idle_when_wet);
    RUN_TEST(test_irr_starts_when_dry);
    RUN_TEST(test_irr_valve_changed_returns_true);
    RUN_TEST(test_irr_soaks_after_on_time);
    RUN_TEST(test_irr_stops_when_wet_after_soak);
    RUN_TEST(test_irr_cycles_when_still_dry);
    RUN_TEST(test_irr_max_cycles_pauses);
    RUN_TEST(test_irr_unpauses_when_wet);
    RUN_TEST(test_irr_disabled_closes_valve);
    RUN_TEST(test_irr_sensor_disconnect_aborts);
    RUN_TEST(test_irr_force_off_during_watering);
    RUN_TEST(test_irr_force_off_during_idle_noop);

    return UNITY_END();
}
