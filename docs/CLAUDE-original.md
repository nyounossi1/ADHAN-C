# CLAUDE.md — A:DHAN AI Adhan Clock

## Project Overview
**A:DHAN AI** is an ESP32-based Islamic prayer time clock (firmware name: ADHAI).  
It displays a live arc/bell-curve OLED visualisation of the Islamic prayer day, plays
the Adhan (call to prayer) at the correct times via a DFPlayer Mini audio module, and
auto-configures itself using WiFi for geolocation, NTP time sync, prayer time API
calls, and OTA firmware updates.

Current firmware version: **0.3.08** | Hardware revision: **Rev-A**

---

## Hardware

| Peripheral | Details |
|---|---|
| MCU | ESP32 Dev Module (Xtensa 240 MHz, 320 KB RAM, 4 MB Flash) |
| Display | SH1106G 128×64 OLED via I²C (addr 0x3C, SDA=21, SCL=22) |
| Audio | DFRobot DFPlayer Mini (UART Serial2, RX=16, TX=17, BUSY=4) |
| Buttons | UP=GPIO35, OK=GPIO32, DN=GPIO25 (all hardware interrupts) |
| LEDs | OK_LED=GPIO26, BTN_LED=GPIO27 |
| DFPlayer power switch | GPIO13 (active HIGH) |

### Key Hardware Notes
- DFPlayer is powered OFF at boot and switched on only for playback (power saving).
- SH1106G I²C runs at 400 kHz.
- ESP32 CPU is throttled to 60–80 MHz at runtime for power saving.
- Bluetooth is disabled via `btStop()` in `enablePm()`.
- Flash usage is currently at ~93.6% — avoid adding large data structures or string
  literals; prefer PROGMEM where possible.

---

## Architecture

### FreeRTOS Task Layout

| Task | Core | Priority | Stack | Responsibility |
|---|---|---|---|---|
| `uiTask` | 1 | 3 | 4096 | UI state machine, menu, OLED drawing |
| `inputTask` | 1 | 3 | 2048 | Debounce, long-press detection, queue |
| `wifiTask` | 0 | 2 | 8192 | WiFi STA/AP, location, NTP, prayers, FOTA |
| `prayerChimeTask` | 1 | 1 | 4096 | Monitor time, trigger Adhan playback |

### Module Files

| File | Responsibility |
|---|---|
| `main.cpp` / `ADHAI_0_3_08.ino` | `setup()`: GPIO, OLED init, RTOS handles, task spawn |
| `Globals.h` / `Globals.cpp` | Shared types, enums, all global variables, settings persistence |
| `UiTask.cpp` / `.h` | UI state machine (8 screen states), menu engine, arc idle renderer integration, ISRs, timers |
| `ArcIdleRenderer.cpp` / `.h` | Bell-curve arc OLED visualisation of the prayer day |
| `PrayerEngine.cpp` / `.h` | Location fetch (ip-api.com), prayer time fetch (Aladhan API), NTP sync, next prayer computation, Qibla bearing |
| `WifiManager.cpp` / `.h` | WiFi STA connect/retry, captive portal (AP mode), session runners |
| `DFPlayerManager.cpp` / `.h` | DFPlayer power, UART init, volume, playback, scheduled power-off timer |
| `FotaManager.cpp` / `.h` | HTTPS firmware version check (AWS S3), download, flash, reboot |

### Screen State Machine (ScreenState enum)
```
SCREEN_SPLASH → SCREEN_ARC_IDLE (default idle)
SCREEN_ARC_IDLE → SCREEN_PRAYER_SCREEN (on prayer time)
SCREEN_ARC_IDLE → SCREEN_MENU_ENGINE (on OK press)
SCREEN_MENU_ENGINE → SCREEN_INFO | SCREEN_CONFIRM | SCREEN_WIFI_STATUS | SCREEN_LIST
SCREEN_WIFI_AP → onboarding flow (captive portal)
SCREEN_FACTORY_RESET → countdown then ESP.restart()
```

### RTOS Communication
- **`uiQueue`** (64 deep) — all UI events (`UiEvent` structs with `UiEventType`)
- **`rawBtnQueue`** (16 deep) — raw button ISR events for `inputTask`
- **`wifiCmdQueue`** (8 deep) — commands to `wifiTask`
- **`g_dataMtx`** — guards `g_lat`, `g_lon`, `g_tzIana`, `g_today`, `g_tomorrow`
- **`g_displayMtx`** — guards all `display.*` calls
- **`g_fotaMtx`** — guards `g_fotaStatus`
- **`g_splashMtx`** — guards `g_splashStatus`
- **`g_logMtx`** — serialises `Serial.printf` across tasks

---

## Key External APIs

| API | Use | Auth |
|---|---|---|
| `http://ip-api.com/json` | Geolocation (lat/lon/timezone) | None |
| `https://api.aladhan.com/v1/timings` | Daily prayer times | None |
| `pool.ntp.org` / `time.nist.gov` / `time.google.com` | NTP | None |
| AWS S3 `my-adhan-firmware.s3.eu-north-1.amazonaws.com` | FOTA version + firmware | AWS Root CA cert pinned |

---

## Settings & Persistence (NVS via Preferences)

| Namespace | Key | Type | Default | Description |
|---|---|---|---|---|
| `settings` | `timeFmt` | uint8 | 1 | 0=12Hr, 1=24Hr |
| `settings` | `adhanType` | uint8 | 0 | 0=Full, 1=Short, 2=Chime |
| `settings` | `dua` | bool | true | Play Dua after Fajr |
| `settings` | `volIdx` | uint8 | 1 | Volume index 0–3 |
| `settings` | `pMethod` | int | 2 | Calculation method (0–23, ISNA=2) |
| `settings` | `pSchool` | int | 1 | Juristic school (0=Shafi, 1=Hanafi) |
| `settings` | `pLatAdj` | int | 3 | Latitude adjustment (0–3) |
| `settings` | `tz` | String | "" | IANA timezone override ("" = auto) |
| `settings` | `fotaLatest` | String | "" | Cached latest firmware version |
| `settings` | `fotaAvail` | bool | false | Update available flag |
| `settings` | `fotaLastDay` | uint32 | 0 | Day key of last FOTA check |
| `settings` | `cntBanner` | bool | true | Show countdown banner on idle |
| `adhan_ai` | `ssid` | String | "" | WiFi SSID |
| `adhan_ai` | `pass` | String | "" | WiFi password |

---

## Audio: DFPlayer Track Mapping

Folder 1 on SD card:
| Track | Adhan Type | Prayer |
|---|---|---|
| 1 | Full | Fajr (special Fajr adhan) |
| 2 | Full | All other prayers |
| 3 | Short | All prayers |
| 4 | Chime | All prayers |

Volume levels: `{ 0, 7, 14, 21 }` → `{ MUTE, LOW, MED, HIGH }`

---

## Coding Standards

### Language & Style
- C++11/Arduino framework; all source files are `.cpp`/`.h` — **not `.ino`** (except legacy `ADHAI_0_3_08.ino` which is being phased into `main.cpp`)
- Always `#include <Arduino.h>` at the top of `.cpp` files
- Include guards: use `#pragma once`
- Use descriptive names; no single-letter variables except loop counters (`i`, `j`)
- Module-private state: declare `static` at file scope
- ISRs: must be `IRAM_ATTR`; minimal work (queue send only)
- Constants: use `#define` for pin numbers; `static constexpr` for class constants; `const` for arrays

### Thread Safety
- **Always** take `g_dataMtx` before reading/writing `g_lat`, `g_lon`, `g_tzIana`, `g_today`, `g_tomorrow`
- **Always** take `g_displayMtx` before any `display.*` call
- ISRs must use `xQueueSendFromISR` / `xSemaphoreGiveFromISR` — never blocking calls
- Prefer snapshot pattern: take mutex, copy data, release mutex, then process

### Memory
- Flash is at 93.6% — avoid large string literals; use `PROGMEM` or move to SD where feasible
- Prefer stack-allocated buffers with explicit sizes (`char buf[16]`) over `String` concatenation in hot paths
- `StaticJsonDocument<N>` is deprecated in ArduinoJson v7 — use `JsonDocument` (migration task open)

### Error Handling
- Always check return values from HTTP calls, JSON parse, and RTOS primitives
- Log errors with `LOGE(tag, ...)` — never silently ignore
- On unrecoverable hardware init failure: `while (true) delay(1000)` with preceding `LOGE`

### Logging
```cpp
LOGI(LOG_TAG_SYS, "message %d", value);   // Info
LOGW(LOG_TAG_WIFI, "warning");             // Warning
LOGE(LOG_TAG_OLED, "error %s", msg);       // Error
```
Log tags: `LOG_TAG_UI`, `LOG_TAG_BTN`, `LOG_TAG_SYS`, `LOG_TAG_OLED`, `LOG_TAG_WIFI`,
`LOG_TAG_NTP`, `LOG_TAG_LOC`, `LOG_TAG_PRAY`, `LOG_TAG_ADHAN`, `LOG_TAG_MENU`

Disable all logging for release builds: set `#define LOG_ENABLED 0` in `Globals.h`.

### Functions
- Keep functions under 60 lines; extract helpers if longer
- Use `snprintf` not `sprintf`; always pass buffer size
- Time values: store as **minutes from midnight** (`int16_t`, range 0–1439)

---

## SDLC Process

### Branching Strategy
```
main        — production-ready, tagged releases only
develop     — integration branch
feature/US-<id>-short-description
bugfix/BUG-<id>-short-description
hotfix/HF-<id>-short-description
```

### Commit Message Format
```
[TYPE] Short description (≤72 chars)

Optional body explaining why, not what.

Refs: US-<id> | BUG-<id>
```
Types: `FEAT`, `FIX`, `REFACTOR`, `DOCS`, `TEST`, `CHORE`

### Definition of Done
- [ ] Build succeeds with zero errors, zero new warnings
- [ ] Flash usage stays below 95%
- [ ] Tested on hardware (or clearly marked as untested)
- [ ] `CLAUDE.md` updated if architecture changes
- [ ] Committed with proper message referencing user story / bug ID

### User Story Format
```
US-<id>: As a <user>, I want <feature>, so that <benefit>.
Acceptance Criteria:
  - <criterion 1>
  - <criterion 2>
```

---

## File Structure
```
project-root/
├── src/
│   ├── ADHAI_0_3_08.ino      # Legacy main (being merged into main.cpp)
│   ├── main.cpp              # setup() and loop()
│   ├── Globals.cpp / .h      # All shared state and utilities
│   ├── UiTask.cpp / .h       # UI, menus, input, chime task
│   ├── ArcIdleRenderer.cpp/.h # OLED arc visualisation
│   ├── PrayerEngine.cpp / .h # Prayer times, location, NTP, Qibla
│   ├── WifiManager.cpp / .h  # WiFi STA/AP/portal
│   ├── DFPlayerManager.cpp/.h # Audio player
│   └── FotaManager.cpp / .h  # OTA updates
├── docs/
│   ├── user-stories.md
│   └── tech-requirements.md
├── platformio.ini
├── CLAUDE.md                 # This file
└── .claudeignore
```

---

## Open Technical Debt
1. **ArduinoJson migration**: Replace all `StaticJsonDocument<N>` with `JsonDocument` in `PrayerEngine.cpp`
2. **Dual main files**: `ADHAI_0_3_08.ino` and `main.cpp` both exist — consolidate into `main.cpp` only
3. **Flash headroom**: At 93.6% — identify largest contributors and optimise
4. **Timezone table**: `IANA_TO_POSIX[]` in `Globals.cpp` covers ~15 regions only — extend or replace with a lookup service
5. **`lib_extra_dirs`** in `platformio.ini` points to a local Desktop path — remove before team sharing

---

## Rules for Claude
- DO NOT modify `IANA_TO_POSIX[]` without also updating the count
- DO NOT add `#include <Arduino.h>` to `.h` files (only `.cpp`)
- DO NOT use `String` concatenation in ISRs or timer callbacks
- DO NOT introduce new global variables without declaring `extern` in `Globals.h` and defining in `Globals.cpp`
- ALWAYS run a mental build check: does the change require a new `lib_deps` entry?
- When adding a new screen state, update both the `ScreenState` enum AND the `uiTask` switch-case
- When suggesting FOTA-related changes, note the flash budget impact