# Tests

Unit tests for pure logic (no hardware dependencies). Uses [PlatformIO](https://platformio.org/) with the [Unity](http://www.throwtheswitch.org/unity) test framework compiled for the **native** platform (runs on the host machine, not on an MCU).

## Test suites

| Suite | File | What it covers |
|---|---|---|
| `test_greenhouse` | [test_greenhouse.cpp](test_greenhouse/test_greenhouse.cpp) | `adcToPercent` conversion, `IrrigationCtrl` state machine |
| `test_time_schedule` | [test_time_schedule.cpp](test_time_schedule/test_time_schedule.cpp) | `isTimeInSpan` time-range logic, weekday filtering, `GreenhouseConfig` defaults |

## Prerequisites

* **PlatformIO Core CLI** — install via `pip install platformio` or from https://docs.platformio.org/en/latest/core/installation.html
* **GCC/G++** — any recent version (Linux: `apt install build-essential`, macOS: Xcode CLI tools, Windows: MinGW-w64)

## Running locally

```bash
cd test
pio test -e native
```

All 36 tests should pass in a few seconds:

```
=================================== SUMMARY ===================================
Environment    Test                Status    Duration
-------------  ------------------  --------  ------------
native         test_greenhouse     PASSED    00:00:02.211
native         test_time_schedule  PASSED    00:00:01.539
================= 36 test cases: 36 succeeded in 00:00:03.749 =================
```

## How it works

The test mock headers in [`test/src/`](src/) provide minimal stubs for Arduino, EEPROM, WiFi, etc. so that `greenhouse_config.h` and `irrigation.h` can compile on a desktop compiler. The tests only exercise pure functions and structs — no hardware is mocked or simulated.
