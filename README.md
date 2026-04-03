# Tank Level

A multi-component IoT system for water tank monitoring, pump control, greenhouse automation, and Nexa smart plug scheduling. Built with PlatformIO on ESP8266 and ESP32 microcontrollers.

## System overview

```
┌─────────────────┐         TCP :15011           ┌──────────────────┐
│  remote_btn     │◄──────────────────────────►  │  tank            │
│  ESP8266 D1 Mini│   PUMP_ENABLE/DISABLE        │  ESP8266 D1 Mini │
│  Pump button    │   PUMP_STATE broadcasts      │  Water level     │
└─────────────────┘                              │  Pump control    │
                                                 │  HTTP server     │
┌─────────────────┐         TCP :15011           │  SD card logging │
│  greenhouse     │◄──────────────────────────►  └──────────────────┘
│  ESP32 S2 Mini  │
│  Light control  │
│  Irrigation     │
│  Nexa plugs     │
└─────────────────┘
```

All nodes connect to the same WiFi network and discover each other via mDNS. The **tank** node acts as the central server — it measures water level, controls the pump relay, and accepts TCP commands from the other nodes. Both **remote_btn** and **greenhouse** connect as TCP clients to send pump requests and receive state updates.

## Components

| Directory | MCU | Purpose |
|---|---|---|
| [`tank/`](tank/) | ESP8266 D1 Mini | Water level sensing (ultrasonic), pump relay, current monitoring (ACS712), HTTP status page, TCP server, SD card history |
| [`greenhouse/`](greenhouse/) | ESP32 Lolin S2 Mini | LDR-based grow light control, capacitive soil moisture sensing, irrigation valve state machine, Nexa smart plug scheduling, web UI |
| [`remote_btn/`](remote_btn/) | ESP8266 D1 Mini | Physical push-button for pump on/off with dual-color LED status |
| [`common/`](common/) | — | Shared library: WiFi setup, ArduinoOTA, syslog logging, TCP client, LED driver, pump state enum |
| [`test/`](test/) | Native (PC) | Unit tests for pure logic (PlatformIO + Unity) |

## Hardware

### Tank

| Pin | Function |
|---|---|
| D3 / D2 | Ultrasonic distance sensor (trigger / echo) |
| A0 | Pump current sensing (ACS712 10A) |
| D0 | Pump relay |
| D4 | Manual pump button |
| D8 | SD card CS |
| D1 | Water flow detection |

- Tank height: 920 mm, sensor offset 40 mm from top
- Pump safety: 15-minute max run, dry-run detection at 2000 mA (30 s debounce)
- 30-day history stored on SD card, 24-hour ring buffer in RAM

### Greenhouse

| Pin | Function |
|---|---|
| GPIO 3 | LDR light sensor (ADC, 10 kΩ voltage divider) |
| GPIO 9 | Capacitive soil moisture sensor (ADC, 1 MΩ pulldown) |
| GPIO 7 | Lamp relay (active-low) |
| GPIO 35 | Irrigation valve relay (active-low) |
| GPIO 16 / 18 | Status LED (green / red) |
| GPIO 33 | Push button |

- Light control: twilight thresholds with hysteresis + 5-minute debounce, time-of-day schedule with weekday filtering
- Irrigation: Gardena micro-drip — configurable watering/soak cycles with safety limit, auto-pause when max cycles reached
- Nexa smart plugs: mDNS discovery (`_systemnexa2._tcp`), HTTP control on port 3000, per-plug time schedules
- Config stored in EEPROM (512 bytes, versioned magic number)

### Remote button

| Pin | Function |
|---|---|
| D2 | Push button |
| D6 / D8 | Status LED (green / red) |

LED states: green = pump idle, green blink = running, red = dry-run, orange blink = disconnected.

## Communication

- **TCP** (port 15011): text-based, line-delimited. Commands: `PUMP_ENABLE`, `PUMP_DISABLE`. Broadcasts: `PUMP_STATE:<n>`. Heartbeat: `PING`/`PONG` every 30 s.
- **mDNS**: `tank.local`, `tank_greenhouse.local`, `tank_button.local`
- **Syslog** (RFC 3164, port 514): all nodes log to a central syslog server
- **NTP**: `europe.pool.ntp.org`, Stockholm timezone (CET/CEST)

## Building

Each component is a standalone PlatformIO project. Shared code lives in `common/` and is referenced via `lib_extra_dirs`.

### Prerequisites

- [PlatformIO Core CLI](https://docs.platformio.org/en/latest/core/installation.html)

### Configuration

Copy the settings template and edit for your network:

```bash
cp common/cfg/settings.template common/cfg/settings.h
```

`settings.h` is gitignored — set your WiFi credentials, syslog server IP, and NTP preferences there.

### Build & upload

```bash
cd tank        # or greenhouse, remote_btn
pio run -e esp            # compile
pio run -e esp -t upload  # OTA upload
```

All nodes use OTA updates over WiFi. First flash must be done via USB.

## Tests

Unit tests cover pure logic extracted into hardware-free structs and functions — no Arduino mocks are exercised.

| Suite | Covers |
|---|---|
| `test_greenhouse` | `adcToPercent()`, `IrrigationCtrl` state machine |
| `test_time_schedule` | `isTimeInSpan()`, weekday filtering, config defaults |

```bash
cd test
pio test -e native    # 36 tests, ~4 seconds
```

Requires GCC/G++ on the host machine. See [test/README.md](test/README.md) for details.

Tests also run automatically on push/PR via [GitHub Actions](.github/workflows/test.yml).

## Project structure

```
tank_level/
├── common/
│   ├── cfg/
│   │   ├── settings.template    # WiFi/syslog/NTP config template
│   │   └── settings.h           # Local config (gitignored)
│   └── src/                     # Shared: WiFi, OTA, Log, Led, TankClient, PumpState
├── tank/
│   ├── src/                     # Tank level, pump control, HTTP + TCP servers
│   └── data/                    # Web UI (index.html, history.html)
├── greenhouse/
│   └── src/                     # Light, irrigation, Nexa, web UI, config
├── remote_btn/
│   └── src/                     # Button + LED, TCP client
├── test/
│   ├── test_greenhouse/         # IrrigationCtrl + adcToPercent tests
│   ├── test_time_schedule/      # Time schedule tests
│   └── src/                     # Minimal header stubs for native compilation
└── .github/workflows/test.yml   # CI
```
